#pragma once
#include "ApplicationState.hpp"
#include "realm/lib/ui/Stack.hpp"
#include "realm/platform/SdlKeyCodes.hpp"

namespace realm::application {

// Event aliases and predicates shared by the layer setups and the frame loop.
using UiEvent = realm::ui::Event;
using UiKind = realm::ui::EventKind;
using UiResult = realm::ui::Result;
using UiReset = realm::ui::Reset;

inline bool left_button(const UiEvent& e) {
  return e.button == SDL_BUTTON_LEFT && (e.kind == UiKind::pointer_down || e.kind == UiKind::pointer_up);
}
inline bool window_left(const UiEvent& e) { return e.kind == UiKind::focus_lost || e.kind == UiKind::pointer_left; }

// The client's screen layers, in ui_stack add() order (bottom to top). Members are declared in add order, so they are
// destroyed in reverse add order, as the old run() locals were. Layers point at each other by address, so this struct
// is neither copied nor moved after configuration; run() owns it through a unique_ptr.
struct ClientLayers {
  realm::ui::FunctionLayer hud_layer{"hud"};
  realm::ui::FunctionLayer minimap_layer{"minimap"};
  realm::ui::FunctionLayer trade_layer{"trade"};
  realm::ui::FunctionLayer arrows_layer{"party-arrows"};
  realm::ui::FunctionLayer player_menu_layer{"player-menu"};
  realm::ui::FunctionLayer group_layer{"player-group"};
  realm::ui::FunctionLayer chat_layer{"chat"};
  realm::ui::FunctionLayer interaction_layer{"interaction"};
  realm::ui::FunctionLayer board_layer{"guild-board"};
  realm::ui::FunctionLayer members_layer{"guild-members"};
  realm::ui::FunctionLayer skins_layer{"skins"};
  realm::ui::FunctionLayer options_layer{"options"};
  realm::ui::FunctionLayer dialog_layer{"dialog"};
  realm::ui::FunctionLayer startup_layer{"startup"};

  ClientLayers() = default;
  ClientLayers(const ClientLayers&) = delete;
  ClientLayers& operator=(const ClientLayers&) = delete;
  ClientLayers(ClientLayers&&) = delete;
  ClientLayers& operator=(ClientLayers&&) = delete;
};

// Each configures one layer's callbacks. Bodies capture by reference over cs and layers; run() adds the layer to
// cs.ui_stack right after its configure call, in the order above.
// ApplicationLayersHud.cpp
void configure_hud_layer(ClientState& cs, ClientLayers& layers);
void configure_minimap_layer(ClientState& cs, ClientLayers& layers);
void configure_arrows_layer(ClientState& cs, ClientLayers& layers);
void configure_player_menu_layer(ClientState& cs, ClientLayers& layers);
void configure_group_layer(ClientState& cs, ClientLayers& layers);
// ApplicationLayersSocial.cpp
void configure_trade_layer(ClientState& cs, ClientLayers& layers);
void configure_chat_layer(ClientState& cs, ClientLayers& layers);
void configure_interaction_layer(ClientState& cs, ClientLayers& layers);
void configure_board_layer(ClientState& cs, ClientLayers& layers);
void configure_members_layer(ClientState& cs, ClientLayers& layers);
// ApplicationLayersScreens.cpp
void configure_skins_layer(ClientState& cs, ClientLayers& layers);
void configure_options_layer(ClientState& cs, ClientLayers& layers);
void configure_dialog_layer(ClientState& cs, ClientLayers& layers);
void configure_startup_layer(ClientState& cs, ClientLayers& layers);

}  // namespace realm::application
