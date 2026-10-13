#include "realm/backdrop.hpp"
#include "realm/telemetry.hpp"
#include "realm/stress.hpp"
#include "realm/scene.hpp"
#include "realm/session.hpp"
#include "realm/movement.hpp"
#include "realm/font.hpp"
#include "realm/hud.hpp"
#include "realm/hud_input.hpp"
#include "realm/tooltip.hpp"
#include "realm/player_tooltip.hpp"
#include "realm/player_menu.hpp"
#include "realm/chat.hpp"
#include "realm/trade.hpp"
#include "realm/dialog.hpp"
#include "realm/guild_board.hpp"
#include "realm/skins.hpp"
#include "realm/guild_members.hpp"
#include "realm/interaction.hpp"
#include "realm/minimap.hpp"
#include "realm/minimap_gpu.hpp"
#include "realm/player_group.hpp"
#include "realm/party_arrows.hpp"
#include "realm/startup.hpp"
#include "application_audio.hpp"
#include "realm/ui_stack.hpp"
#include "realm/ui_retained.hpp"
#include "realm/startup_rml.hpp"
#include "realm/text_tooltip.hpp"
#include "realm/credit_display.hpp"
#include "realm/safe_area_display.hpp"
#include "realm/options.hpp"
#include "realm/sdl_keys.hpp"
#include "realm/settings.hpp"
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

#include "realm/application.hpp"
#include "realm/platform.hpp"
#include "realm/platform_sdl.hpp"
#include "application_options.hpp"
#include "application_script.hpp"
#include "application_state.hpp"
#include "application_layers.hpp"
#include "application_loop.hpp"

namespace realm::application {
int run(int argc, char** argv) {
  try {
    auto owner = std::make_unique<ClientState>(parse(argc, argv));
    auto& cs = *owner;
    const auto& o = cs.o;
    auto layers = std::make_unique<ClientLayers>();

    // Every screen is a layer of one input stack, added bottom to top in display order. The stack owns
    // modal blocking, pointer capture (a press is released where it began), text focus, the world's pointer gate and
    // the resets on map change and session end; the layers below keep each screen's own input rules.
    configure_hud_layer(cs, *layers);
    cs.ui_stack.add(layers->hud_layer);

    configure_minimap_layer(cs, *layers);
    cs.ui_stack.add(layers->minimap_layer);

    configure_trade_layer(cs, *layers);
    cs.ui_stack.add(layers->trade_layer);

    configure_arrows_layer(cs, *layers);
    cs.ui_stack.add(layers->arrows_layer);

    configure_player_menu_layer(cs, *layers);
    cs.ui_stack.add(layers->player_menu_layer);

    configure_group_layer(cs, *layers);
    cs.ui_stack.add(layers->group_layer);

    configure_chat_layer(cs, *layers);
    cs.ui_stack.add(layers->chat_layer);

    configure_interaction_layer(cs, *layers);
    cs.ui_stack.add(layers->interaction_layer);

    configure_board_layer(cs, *layers);
    cs.ui_stack.add(layers->board_layer);

    configure_members_layer(cs, *layers);
    cs.ui_stack.add(layers->members_layer);

    configure_skins_layer(cs, *layers);
    cs.ui_stack.add(layers->skins_layer);

    configure_options_layer(cs, *layers);
    cs.ui_stack.add(layers->options_layer);

    configure_dialog_layer(cs, *layers);
    cs.ui_stack.add(layers->dialog_layer);

    configure_startup_layer(cs, *layers);
    cs.ui_stack.add(layers->startup_layer);

    while (cs.running && (!o.frames || cs.frame < static_cast<std::uint64_t>(o.frames))) {
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
      if (!prepare_frame(cs, f)) continue;
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
    shutdown_report(cs, o);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "realm_client: " << error.what() << '\n';
    return 1;
  }
}
}
