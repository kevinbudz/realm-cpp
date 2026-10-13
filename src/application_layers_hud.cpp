#include "application_layers.hpp"

namespace realm::application {

// HUD sidebar: inventory, panel buttons, party panel and header. It sees every pointer move (the menus above it
// take only presses), so hover and tooltips follow the pointer everywhere.
void configure_hud_layer(ClientState& cs, ClientLayers& layers) {
  const auto& o = cs.o;
  auto& hud_layer = layers.hud_layer;
  hud_layer.is_active = [&] { return static_cast<bool>(cs.hud); };
  hud_layer.is_covering = [&](realm::Vec2 p) {
    return o.world && cs.ui_viewport.valid() && p.x >= cs.ui_viewport.world_right();
  };
  hud_layer.on_cancel = [&] {
    cs.hud_input.cancel();
    cs.hud_mouse_down = false;
    cs.tooltip_pointer_valid = false;
    cs.header_pressed = false;
    cs.party_hover.reset();
  };
  hud_layer.on_reset = [&](UiReset reason) {
    cs.party_hover.reset();
    cs.hud_view.party = {};
    cs.tooltip_pointer_valid = false;
    cs.balloons.clear();
    cs.status_texts.clear();
    if (reason == UiReset::session_end) {
      cs.hud_input.cancel();
      cs.hud_container = -1;
    }
  };
  hud_layer.on_event = [&](const UiEvent& e) -> UiResult {
    switch (e.kind) {
      case UiKind::pointer_left:
        cs.tooltip_pointer_valid = false;
        return UiResult::pass;
      case UiKind::focus_lost:
        cs.tooltip_pointer_valid = false;
        cs.hud_input.cancel();
        cs.hud_mouse_down = false;
        return UiResult::pass;
      case UiKind::resized: {
        // A previously hovered field may have moved beneath a stationary cursor.
        cs.tooltip_pointer_valid = false;
        int mx{}, my{};
        SDL_GetMouseState(&mx, &my);
        if (e.viewport.valid())
          cs.hud_input.move(e.viewport.sidebar(e.viewport.drawable(mx, my)), cs.hud_input.selected(),
                            cs.hud_container >= 0);
        return UiResult::pass;
      }
      case UiKind::wheel:
        return UiResult::pass;
      case UiKind::pointer_move:
        cs.tooltip_pointer_valid = true;
        break;
      case UiKind::pointer_down:
      case UiKind::pointer_up:
        if (e.button == SDL_BUTTON_LEFT) cs.hud_mouse_down = e.kind == UiKind::pointer_down;
        break;
      default:
        return UiResult::pass;
    }
    const auto* player =
        cs.hud_art && o.world && cs.ui_viewport.valid() ? cs.world.entity(cs.world.player_id()) : nullptr;
    if (!player) return UiResult::pass;
    const auto point = cs.ui_viewport.sidebar(e.point);
    auto state = realm::HudState::player(cs.world, *player).inventory;
    if (state.container != cs.hud_container) {
      cs.hud_input.container_changed();
      cs.hud_container = state.container;
    }
    if (e.kind == UiKind::pointer_move) {
      cs.hud_input.move(point, cs.hud_input.selected(), state.container >= 0);
      return UiResult::pass;
    }
    if (e.button != SDL_BUTTON_LEFT) return UiResult::pass;
    const bool down = e.kind == UiKind::pointer_down;
    const auto& panel = cs.hud_view;
    const auto button = panel.panel_button(cs.fonts);
    // PartyPanel.onMouseDown replaces any open menu with one for the member clicked.
    if (down && cs.player_menu && cs.game_session && panel.party.shown)
      if (const int cell = realm::PartyHover::cell(point); cell >= 0) {
        std::int32_t id = cs.party_hover.target();
        if (id < 0 && panel.party.members.at(static_cast<std::size_t>(cell)))
          id = panel.party.members.at(static_cast<std::size_t>(cell))->object_id;
        cs.player_menu->close();
        if (cs.player_group) cs.player_group->close();
        if (const auto target = realm::player_menu_target(cs.world, cs.game_session->party(), id,
                                                          cs.game_session->map_info().allow_teleport))
          if (cs.player_menu->open(*target, e.point, cs.ui_viewport.drawable_width, cs.ui_viewport.drawable_height,
                                   cs.ui_viewport.scale()))
            cs.player_menu_texture.update(cs.player_menu->image());
      }
    // CharacterDetailsView's IconButton: Options in the Nexus, otherwise escape to the Nexus.
    const bool header_over = panel.header.button != realm::HudState::Header::Button::none && !cs.trade.active() &&
                             realm::HudInput::button_at(realm::header_button, point);
    if (down)
      cs.header_pressed = header_over;
    else if (std::exchange(cs.header_pressed, false) && header_over) {
      if (panel.header.button == realm::HudState::Header::Button::options)
        cs.open_options();
      else if (cs.game_session)
        cs.game_session->escape();
    }
    if (down)
      cs.hud_input.button_down(point, button);
    else if (cs.hud_input.button_up(point, button) && cs.game_session) {
      if (panel.portal.id == button.id)
        cs.game_session->use_portal(button.id);
      else if (panel.social.id == button.id) {
        if (panel.social.kind == realm::HudState::SocialKind::hall)
          cs.game_session->use_portal(button.id);
        else if (cs.interaction.open(panel.social, *player)) {
          cs.hud_input.cancel();
          cs.hud_mouse_down = false;
          cs.tooltip_pointer_valid = false;
        }
      } else if (panel.sellable.id == button.id)
        cs.game_session->buy(button.id);
      // GuildBoardPanel opens with the current authoritative rank.
      else if (panel.button.kind == 0) {
        cs.board.open(cs.app_host, o.app_port, cs.game_session->credentials(),
                      player->profile.stats.value_or(realm::flash::StatId::GUILD_RANK_STAT, 0));
        cs.chat.close();
        if (cs.player_menu) cs.player_menu->close();
        if (cs.player_group) cs.player_group->close();
        cs.party_arrows.reset();
      } else if (panel.button.kind == 1) {
        cs.members.open(cs.app_host, o.app_port, cs.game_session->credentials(), player->profile.guild,
                        player->profile.name, player->profile.stats.value_or(realm::flash::StatId::GUILD_RANK_STAT, 0));
        cs.chat.close();
        if (cs.player_menu) cs.player_menu->close();
        if (cs.player_group) cs.player_group->close();
        cs.party_arrows.reset();
      }
      // CharacterChangerPanel dispatches GameSprite.closed.
      else if (panel.button.kind == 3 && cs.skins) {
        cs.skins->endpoint(cs.app_host, o.app_port, cs.game_session->credentials());
        if (cs.startup && cs.startup->char_list())
          cs.skins->open(player->type, player->appearance.texture, *cs.startup->char_list());
        else
          cs.skins->fetch(player->type, player->appearance.texture, cs.app_host, o.app_port,
                          cs.game_session->credentials());
        cs.chat.close();
        if (cs.player_menu) cs.player_menu->close();
        if (cs.player_group) cs.player_group->close();
        cs.party_arrows.reset();
      } else if (panel.button.kind == 2 && cs.startup) {
        cs.leaving_game = true;
        cs.game_session->disconnect();
      }
    }
    if (down)
      cs.hud_input.down(point, state);
    else if (const auto action = cs.hud_input.up(point, (SDL_GetModState() & KMOD_SHIFT) != 0,
                                                 (SDL_GetModState() & KMOD_CTRL) != 0, cs.ui_time, state);
             action && cs.game_session) {
      const auto slot = [&](int index) {
        return index >= realm::container_slot
                   ? realm::net::Slot{state.container, static_cast<std::uint8_t>(index - realm::container_slot)}
                   : realm::net::Slot{player->id, static_cast<std::uint8_t>(index)};
      };
      const auto source = slot(action->source);
      switch (action->kind) {
        case realm::InventoryActionKind::swap:
          cs.game_session->swap_items(source, slot(action->target), cs.ui_time);
          break;
        // ItemGridMediator.dropItem sends nothing for container tiles.
        case realm::InventoryActionKind::drop:
          if (action->source < realm::container_slot) cs.game_session->drop_to_ground(action->source, cs.ui_time);
          break;
        case realm::InventoryActionKind::use:
          cs.game_session->use_item(source, cs.ui_time);
          break;
        case realm::InventoryActionKind::equip:
          cs.game_session->equip_or_use(source, cs.ui_time);
          break;
        case realm::InventoryActionKind::potion:
          cs.game_session->use_potion(action->source, cs.ui_time);
          break;
        case realm::InventoryActionKind::transfer:
        case realm::InventoryActionKind::store: {
          const bool transfer = action->kind == realm::InventoryActionKind::transfer;
          if (transfer && !state.backpack) break;
          const int begin = transfer && cs.hud_input.selected() != 2 ? 12 : 4,
                    end = transfer ? begin + 8 : (state.backpack ? 20 : 12);
          for (int i = begin; i < end; ++i)
            if (i != action->source && state.items.at(static_cast<std::size_t>(i)) <= 0) {
              cs.game_session->swap_items(source, {player->id, static_cast<std::uint8_t>(i)}, cs.ui_time);
              break;
            }
          break;
        }
      }
    }
    // MapUserInput listens on the map: a press that lands on the sidebar holds the pointer and never fires.
    return down && hud_layer.covers(e.point) ? UiResult::capture : UiResult::pass;
  };
}

namespace {
// The minimap's closures outlive configure_minimap_layer, so these are file-scope helpers. The viewport is
// passed in rather than captured from a local lambda.
realm::Vec2 minimap_point(const realm::ui::Viewport& viewport, realm::Vec2 drawable) {
  const auto p = viewport.sidebar(drawable);
  return realm::Vec2{p.x - 4, p.y - 4};
}
bool minimap_inside(realm::Vec2 m) { return m.x >= 0 && m.x < 192 && m.y >= 0 && m.y < 192; }
}  // namespace

// Minimap: 192 px square, 4 px in from the sidebar's top-left; zoom buttons on press, a group menu on release.
void configure_minimap_layer(ClientState& cs, ClientLayers& layers) {
  const auto& o = cs.o;
  auto& minimap_layer = layers.minimap_layer;
  minimap_layer.is_active = [&] { return o.world && static_cast<bool>(cs.minimap); };
  minimap_layer.is_covering = [&](realm::Vec2 p) {
    return cs.ui_viewport.valid() && minimap_inside(minimap_point(cs.ui_viewport, p));
  };
  minimap_layer.on_event = [&](const UiEvent& e) -> UiResult {
    if (!left_button(e) || !cs.ui_viewport.valid()) return UiResult::pass;
    const auto m = minimap_point(cs.ui_viewport, e.point);
    const bool inside = minimap_inside(m), zoom_button = m.x >= 172 && m.x < 188 && m.y >= 4 && m.y < 30;
    if (e.kind == UiKind::pointer_down) {
      if (!inside) return UiResult::pass;
      if (zoom_button) {
        if (m.y >= 14)
          cs.minimap->zoom_out();
        else
          cs.minimap->zoom_in();
      }
      return UiResult::capture;
    }
    if (cs.ui_stack.capture() != &minimap_layer) return UiResult::pass;
    if (inside && !zoom_button && cs.player_group && cs.player_group->visible() && !cs.player_group->menu() &&
        cs.game_session) {
      auto players = cs.player_group->players();
      if (cs.player_menu) cs.player_menu->close();
      if (cs.player_group->open(std::move(players), cs.game_session->map_info().allow_teleport, e.point,
                                cs.ui_viewport.drawable_width, cs.ui_viewport.drawable_height, cs.ui_viewport.scale()))
        cs.group_texture.update(cs.player_group->image());
    }
    return UiResult::consumed;
  };
}

// Party arrows at the playfield edge open the group menu for the players they point at.
void configure_arrows_layer(ClientState& cs, ClientLayers& layers) {
  auto& arrows_layer = layers.arrows_layer;
  arrows_layer.is_active = [&] { return cs.player_group && cs.game_session; };
  arrows_layer.is_covering = [&](realm::Vec2 p) { return cs.party_arrows.hit(p) != nullptr; };
  arrows_layer.on_reset = [&](UiReset) { cs.party_arrows.reset(); };
  arrows_layer.on_event = [&](const UiEvent& e) -> UiResult {
    if (e.kind == UiKind::focus_lost) {
      cs.party_arrows.reset();
      return UiResult::pass;
    }
    if (!left_button(e)) return UiResult::pass;
    if (e.kind == UiKind::pointer_up)
      return cs.ui_stack.capture() == &arrows_layer ? UiResult::consumed : UiResult::pass;
    if (cs.player_menu && cs.player_menu->contains(e.point)) return UiResult::pass;
    const auto* arrow = cs.party_arrows.hit(e.point);
    if (!arrow) return UiResult::pass;
    auto players = realm::player_group_targets(cs.world, arrow->players);
    if (cs.player_menu) cs.player_menu->close();
    if (cs.player_group->open(std::move(players), cs.game_session->map_info().allow_teleport, e.point,
                              cs.ui_viewport.drawable_width, cs.ui_viewport.drawable_height, cs.ui_viewport.scale()))
      cs.group_texture.update(cs.player_group->image());
    return UiResult::capture;
  };
}

// PlayerMenu sits on the stage above the HUD and receives its clicks first.
void configure_player_menu_layer(ClientState& cs, ClientLayers& layers) {
  auto& player_menu_layer = layers.player_menu_layer;
  player_menu_layer.is_active = [&] { return cs.player_menu && cs.player_menu->visible(); };
  player_menu_layer.is_covering = [&](realm::Vec2 p) { return cs.player_menu->contains(p); };
  player_menu_layer.on_reset = [&](UiReset) {
    if (cs.player_menu) cs.player_menu->close();
  };
  player_menu_layer.on_event = [&](const UiEvent& e) -> UiResult {
    if (!left_button(e)) return UiResult::pass;
    if (e.kind == UiKind::pointer_down)
      return cs.player_menu->pointer_down(e.point) ? UiResult::capture : UiResult::pass;
    if (cs.ui_stack.capture() != &player_menu_layer) return UiResult::pass;
    if (const auto request = cs.player_menu->pointer_up(e.point); request && cs.game_session) {
      using A = realm::PlayerMenuAction;
      switch (request->action) {
        case A::teleport:
          if (const auto refusal = cs.game_session->teleport(request->object_id, cs.ui_time))
            cs.chat.add(std::string(realm::error_chat_name), *refusal, cs.ui_time);
          break;
        case A::invite:
          cs.game_session->guild_invite(request->name);
          break;
        case A::trade:
          cs.game_session->request_trade(request->name);
          break;
        case A::lock:
          cs.game_session->edit_account_list(request->object_id, false, true);
          break;
        case A::unlock:
          cs.game_session->edit_account_list(request->object_id, false, false);
          break;
        case A::ignore:
          cs.game_session->edit_account_list(request->object_id, true, true);
          break;
        case A::unignore:
          cs.game_session->edit_account_list(request->object_id, true, false);
          break;
      }
    }
    return UiResult::consumed;
  };
}

// The stacked-player group: a hover tooltip, or a menu whose Teleport row teleports to the first eligible player.
void configure_group_layer(ClientState& cs, ClientLayers& layers) {
  auto& group_layer = layers.group_layer;
  group_layer.is_active = [&] { return cs.player_group && cs.player_group->visible(); };
  group_layer.is_covering = [&](realm::Vec2 p) { return cs.player_group->contains(p); };
  group_layer.on_reset = [&](UiReset) {
    if (cs.player_group) cs.player_group->close();
  };
  group_layer.on_event = [&](const UiEvent& e) -> UiResult {
    if (e.kind == UiKind::focus_lost) {
      cs.player_group->focus_lost();
      return UiResult::pass;
    }
    if (!left_button(e) || !cs.game_session) return UiResult::pass;
    if (e.kind == UiKind::pointer_down)
      return cs.player_group->menu() && cs.player_group->pointer_down(e.point) ? UiResult::capture : UiResult::pass;
    if (cs.ui_stack.capture() != &group_layer) return UiResult::pass;
    if (cs.player_group->pointer_up(e.point)) {
      if (const auto id = realm::first_teleport_player(cs.world, cs.player_group->players())) {
        if (const auto refusal = cs.game_session->teleport(*id, cs.ui_time))
          cs.chat.add(std::string(realm::error_chat_name), *refusal, cs.ui_time);
      } else
        cs.chat.add(std::string(realm::error_chat_name), std::string(realm::no_teleport_players), cs.ui_time);
      cs.player_group->close();
    }
    return UiResult::consumed;
  };
}

}  // namespace realm::application
