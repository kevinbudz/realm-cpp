#include "realm/background/Backdrop.hpp"
#include "realm/util/Telemetry.hpp"
#include "realm/fixtures/StressTimeline.hpp"
#include "realm/map/Scene.hpp"
#include "realm/messaging/Session.hpp"
#include "realm/objects/Movement.hpp"
#include "realm/assets/FontBook.hpp"
#include "realm/hud/Hud.hpp"
#include "realm/hud/HudInput.hpp"
#include "realm/ui/tooltip/ItemTooltip.hpp"
#include "realm/ui/tooltip/PlayerTooltip.hpp"
#include "realm/ui/menu/PlayerMenu.hpp"
#include "realm/ui/Chat.hpp"
#include "realm/ui/Trade.hpp"
#include "realm/ui/dialogs/Dialog.hpp"
#include "realm/ui/board/GuildBoard.hpp"
#include "realm/ui/SkinChooser.hpp"
#include "realm/ui/guild/GuildMembers.hpp"
#include "realm/ui/dialogs/InteractionDialog.hpp"
#include "realm/minimap/Minimap.hpp"
#include "realm/minimap/MinimapGpu.hpp"
#include "realm/ui/menu/PlayerGroup.hpp"
#include "realm/map/partyoverlay/PartyArrows.hpp"
#include "realm/screens/Startup.hpp"
#include "ApplicationAudio.hpp"
#include "realm/lib/ui/Stack.hpp"
#include "realm/lib/ui/RetainedUi.hpp"
#include "realm/screens/StartupRml.hpp"
#include "realm/ui/tooltip/TextToolTip.hpp"
#include "realm/hud/CreditDisplay.hpp"
#include "realm/hud/SafeAreaDisplay.hpp"
#include "realm/ui/options/OptionsScreen.hpp"
#include "realm/platform/SdlKeyCodes.hpp"
#include "realm/parameters/Settings.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <deque>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <set>
#include <vector>

#include "realm/core/Application.hpp"
#include "realm/platform/Platform.hpp"
#include "realm/platform/PlatformSdl.hpp"
#include "ApplicationOptions.hpp"
#include "ApplicationScript.hpp"
#include "ApplicationState.hpp"
#include "ApplicationLayers.hpp"
#include "ApplicationLoop.hpp"
#if defined(__EMSCRIPTEN__)
#include "realm/appengine/SavedCharactersList.hpp"
#include "realm/assets/Image.hpp"
#include "realm/stage3D/WebGpuStart.hpp"
#include <emscripten.h>
#endif

namespace realm::application {
namespace {
// Every screen is a layer of one input stack, added bottom to top in display order. The stack owns
// modal blocking, pointer capture (a press is released where it began), text focus, the world's
// pointer gate and the resets on map change and session end; the layers below keep each screen's
// own input rules.
void add_layers(ClientState& cs, ClientLayers& layers) {
  configure_hud_layer(cs, layers);
  cs.ui_stack.add(layers.hud_layer);

  configure_minimap_layer(cs, layers);
  cs.ui_stack.add(layers.minimap_layer);

  configure_trade_layer(cs, layers);
  cs.ui_stack.add(layers.trade_layer);

  configure_arrows_layer(cs, layers);
  cs.ui_stack.add(layers.arrows_layer);

  configure_player_menu_layer(cs, layers);
  cs.ui_stack.add(layers.player_menu_layer);

  configure_group_layer(cs, layers);
  cs.ui_stack.add(layers.group_layer);

  configure_chat_layer(cs, layers);
  cs.ui_stack.add(layers.chat_layer);

  configure_interaction_layer(cs, layers);
  cs.ui_stack.add(layers.interaction_layer);

  configure_board_layer(cs, layers);
  cs.ui_stack.add(layers.board_layer);

  configure_members_layer(cs, layers);
  cs.ui_stack.add(layers.members_layer);

  configure_skins_layer(cs, layers);
  cs.ui_stack.add(layers.skins_layer);

  configure_options_layer(cs, layers);
  cs.ui_stack.add(layers.options_layer);

  configure_dialog_layer(cs, layers);
  cs.ui_stack.add(layers.dialog_layer);

  configure_startup_layer(cs, layers);
  cs.ui_stack.add(layers.startup_layer);
}

// The loop runs while the client is running and any finite frame count has not been reached.
bool keep_running(const ClientState& cs, const Options& o) {
  return cs.running && (!o.frames || cs.frame < static_cast<std::uint64_t>(o.frames));
}

// One pass of the loop. A frame with no drawable (a minimized window) is not counted.
void run_frame(ClientState& cs, const Options& o) {
  Frame f;

  begin_frame(cs, o, f);

  drive_stress_fixture(cs, f);
  update_session(cs, f);
  update_panels(cs, f);

  sync_text_input(cs);
  run_script_steps(cs, f);
  update_panel_pixels(cs, f);

  dispatch_events(cs, o, f);
  after_dispatch(cs);
  apply_input(cs, o, f);

  if (!prepare_frame(cs, f)) {
    return;
  }

  build_scene(cs, o, f);
  build_hud_overlays(cs, o, f);
  build_group_overlays(cs, o, f);
  build_modal_overlays(cs, o, f);

  present_frame(cs, o, f);
#ifdef REALM_TRACY
  FrameMark;
#endif
  ++cs.frame;
}

#if defined(__EMSCRIPTEN__)
// The web loop's state. The browser calls web_frame once per animation frame, after run() has
// unwound (simulate_infinite_loop throws out of main), so this lives in static storage, not on run()'s
// stack.
struct WebApp {
  std::optional<Options> pending;  // parsed options, until the client is built
  std::unique_ptr<ClientState> owner;
  std::unique_ptr<ClientLayers> layers;
  std::uint64_t ticks{};
  bool gpu_start_checked{};  // WebGPU's adapter and device were requested (when chosen)
  bool capture_pending{};  // the final frame's presented WebGPU readback has not been written yet
  std::uint64_t capture_ticks{};  // web_frame calls since that readback started
};

// Flushes settings and account writes to IndexedDB about every two seconds: the account file is
// written by screens that do not reach the client state.
constexpr std::uint64_t persist_interval_ticks = 120;
// A readback resolves within a few frames in a foreground tab; past this the capture has failed.
constexpr std::uint64_t capture_timeout_ticks = 600;

void web_frame(void* argument) {
  auto& app = *static_cast<WebApp*>(argument);
  try {
    if (!app.owner) {
      // The settings load in the client's constructor, so the client waits for the saved files.
      if (!realm::platform::storage_ready()) {
        return;
      }
      // WebGPU's adapter and device arrive asynchronously, so the window waits for them here (the
      // same kind of gate as the storage). A failed start is recorded by open_window, which then opens WebGL2.
      if (!app.gpu_start_checked) {
        app.gpu_start_checked = true;
        if (startup_api(*app.pending) == realm::gfx::Api::webgpu) {
          realm::gfx::begin_webgpu_start();
        }
      }
      if (realm::gfx::webgpu_start_state() == realm::gfx::WebGpuStart::requesting) {
        return;
      }
      app.owner = std::make_unique<ClientState>(std::move(*app.pending));
      app.pending.reset();
      app.layers = std::make_unique<ClientLayers>();
      add_layers(*app.owner, *app.layers);
      return;
    }
    auto& cs = *app.owner;
    const auto& o = cs.o;
    if (!keep_running(cs, o)) {
      if (app.capture_pending) {
        // The loop stays up (no frames drawn) until the readback of the final frame is written.
        if (auto image = cs.renderer.take_capture()) {
          realm::save_png(o.capture, *image);
          app.capture_pending = false;
        } else if (++app.capture_ticks > capture_timeout_ticks) {
          throw std::runtime_error("WebGPU capture did not complete");
        } else {
          return;
        }
      }
      emscripten_cancel_main_loop();
      shutdown_report(cs, o);
      app.layers.reset();
      app.owner.reset();
      return;
    }
    run_frame(cs, o);
    if (web_presented_capture(o) && cs.frame == static_cast<std::uint64_t>(o.frames)) {
      // The final frame has been presented, so its copy is recorded: start the readback now.
      int width{};
      int height{};
      cs.session.drawable_size(width, height);
      static_cast<void>(cs.renderer.capture(width, height, true));
      app.capture_pending = true;
    }
    if (++app.ticks % persist_interval_ticks == 0) {
      realm::platform::persist_storage();
    }
  } catch (const std::exception& error) {
    std::cerr << "realm_client: " << error.what() << '\n';
    emscripten_cancel_main_loop();
  }
}
#endif
}  // namespace

int run(int argc, char** argv) {
  try {
#if defined(__EMSCRIPTEN__)
    // The browser drives the loop: parse now, start loading the saved files, and let web_frame build
    // the client once they are in place. Frame pacing is requestAnimationFrame (fps 0).
    static WebApp app;
    app.pending = parse(argc, argv);
    realm::platform::mount_storage(realm::account_path().parent_path());
    emscripten_set_main_loop_arg(web_frame, &app, 0, 1);
    return 0;
#else
    auto owner = std::make_unique<ClientState>(parse(argc, argv));
    auto& cs = *owner;
    const auto& o = cs.o;
    auto layers = std::make_unique<ClientLayers>();
    add_layers(cs, *layers);

    while (keep_running(cs, o)) {
      run_frame(cs, o);
    }

    shutdown_report(cs, o);
    return 0;
#endif
  } catch (const std::exception& error) {
    std::cerr << "realm_client: " << error.what() << '\n';
    return 1;
  }
}
}
