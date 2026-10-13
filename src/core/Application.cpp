#include "realm/background/Backdrop.hpp"
#include "realm/util/Telemetry.hpp"
#include "realm/fixtures/StressTimeline.hpp"
#include "realm/map/Scene.hpp"
#include "realm/messaging/Session.hpp"
#include "realm/objects/Movement.hpp"
#include "realm/ui/fonts/FontBook.hpp"
#include "realm/hud/Hud.hpp"
#include "realm/hud/HudInput.hpp"
#include "realm/ui/tooltip/ItemTooltip.hpp"
#include "realm/ui/tooltip/PlayerTooltip.hpp"
#include "realm/ui/menu/PlayerMenu.hpp"
#include "realm/ui/Chat.hpp"
#include "realm/ui/Trade.hpp"
#include "realm/ui/dialogs/Dialog.hpp"
#include "realm/ui/board/GuildBoard.hpp"
#include "realm/classes/SkinChooser.hpp"
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
#include "realm/game/view/CreditDisplay.hpp"
#include "realm/game/view/SafeAreaDisplay.hpp"
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
