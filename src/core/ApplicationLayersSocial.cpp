#include "ApplicationLayers.hpp"

namespace realm::application {

// TradePanel covers the HUD below the minimap; TradeRequestPanel covers the interaction panel.
void configure_trade_layer(ClientState& cs, ClientLayers& layers) {
  auto& trade_layer = layers.trade_layer;
  trade_layer.is_active = [&] { return cs.trade_view && cs.game_session && (cs.trade.active() || cs.trade.request()); };
  trade_layer.is_covering = [&](realm::Vec2 p) {
    if (!cs.ui_viewport.valid()) return false;
    const auto s = cs.ui_viewport.sidebar(p);
    return s.x >= 0 && s.y >= (cs.trade.active() ? realm::TradeView::panel_top : realm::TradeView::request_top);
  };
  trade_layer.on_cancel = [&] { cs.trade_pressed = -1; };
  trade_layer.on_reset = [&](UiReset) {
    cs.trade.end();
    cs.trade_pressed = -1;
  };
  trade_layer.on_event = [&](const UiEvent& e) -> UiResult {
    if (!left_button(e)) return UiResult::pass;
    const bool down = e.kind == UiKind::pointer_down;
    if (!trade_layer.covers(e.point) && (down || cs.ui_stack.capture() != &trade_layer)) return UiResult::pass;
    const auto point = cs.ui_viewport.sidebar(e.point);
    const auto hit = [&](realm::HudRect area) {
      return point.x >= area.x && point.x < area.x + area.width && point.y >= area.y && point.y < area.y + area.height;
    };
    if (cs.trade.active()) {
      if (down) {
        cs.trade_pressed = hit(realm::TradeView::cancel_button) ? 0 : hit(realm::TradeView::trade_button) ? 1 : -1;
        // TradeInventory toggles offers on MOUSE_DOWN.
        if (const int slot = realm::TradeView::slot_at(point); slot >= 0)
          if (const auto offer = cs.trade.toggle(static_cast<std::size_t>(slot), cs.ui_time))
            cs.game_session->change_trade(*offer);
      } else {
        const int pressed = std::exchange(cs.trade_pressed, -1);
        if (pressed == 0 && hit(realm::TradeView::cancel_button)) {
          cs.game_session->cancel_trade();
          cs.trade.end();
        }
        if (pressed == 1 && hit(realm::TradeView::trade_button))
          if (const auto offers = cs.trade.accept(cs.ui_time))
            cs.game_session->accept_trade(offers->first, offers->second);
      }
    } else if (cs.trade.request()) {
      const auto buttons = cs.trade_view->request_buttons();
      if (down)
        cs.trade_pressed = hit(buttons.reject) ? 2 : hit(buttons.accept) ? 3 : -1;
      else {
        const int pressed = std::exchange(cs.trade_pressed, -1);
        if (pressed == 2 && hit(buttons.reject)) cs.trade.close_request();
        // Accepting sends the reverse request that starts the trade.
        if (pressed == 3 && hit(buttons.accept)) {
          cs.game_session->request_trade(*cs.trade.request());
          cs.trade.close_request();
        }
      }
    }
    return down ? UiResult::capture : UiResult::consumed;
  };
}

// TextBox: chat keys act on release; an open field takes all keyboard input. The keys that open it, start a
// command or tell, and scroll are the Chat tab's bindings.
void configure_chat_layer(ClientState& cs, ClientLayers& layers) {
  const auto& o = cs.o;
  auto& chat_layer = layers.chat_layer;
  chat_layer.is_active = [&] {
    return cs.chat_view && o.world && cs.game_session && cs.game_session->state() == realm::SessionState::playing;
  };
  chat_layer.is_text = [&] { return cs.chat.typing(); };
  chat_layer.on_reset = [&](UiReset) { cs.chat.close(); };
  chat_layer.on_event = [&](const UiEvent& e) -> UiResult {
    if (e.kind != UiKind::key_down && e.kind != UiKind::key_up && e.kind != UiKind::text) return UiResult::pass;
    const auto key = e.kind == UiKind::text ? SDLK_UNKNOWN : static_cast<SDL_Keycode>(e.key);
    const int code = realm::sdl_keys::flash_code(key);
    const auto bound = [&](std::string_view param) { return code != 0 && code == cs.settings.get(param); };
    const bool shift = e.kind != UiKind::text && (e.modifiers & KMOD_SHIFT) != 0;
    if (e.kind == UiKind::key_up && bound("scrollChatUp")) cs.chat.scroll_up();
    if (e.kind == UiKind::key_up && bound("scrollChatDown")) cs.chat.scroll_down();
    if (cs.chat.typing()) {
      if (e.kind == UiKind::text)
        cs.chat.text(e.text);
      else if (e.kind == UiKind::key_down && key == SDLK_BACKSPACE)
        cs.chat.backspace();
      // Escape only closes the field; it does not leave the game while typing.
      else if (e.kind == UiKind::key_down && key == SDLK_ESCAPE)
        cs.chat.close();
      else if (e.kind == UiKind::key_up && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
        if (const auto line = cs.chat.submit()) cs.game_session->player_text(*line);
      } else if (e.kind == UiKind::key_up && bound("tell"))
        cs.chat.tell(shift);
      return UiResult::consumed;
    }
    if (e.kind == UiKind::key_up) {
      if (bound("chat"))
        cs.chat.open();
      else if (bound("chatCommand"))
        cs.chat.open("/");
      else if (bound("tell"))
        cs.chat.tell(shift);
      else if (bound("guildChat"))
        cs.chat.open("/g ");
    }
    return UiResult::pass;
  };
}

// The modal panels, lowest first. A modal swallows every event, handled or not.
void configure_interaction_layer(ClientState& cs, ClientLayers& layers) {
  auto& interaction_layer = layers.interaction_layer;
  interaction_layer.is_active = [&] { return cs.interaction.visible(); };
  interaction_layer.is_modal = [] { return true; };
  interaction_layer.is_text = [&] { return cs.interaction.wants_text(); };
  interaction_layer.on_reset = [&](UiReset reason) {
    if (reason == UiReset::session_end) cs.interaction.reset();
  };
  interaction_layer.on_event = [&](const UiEvent& e) -> UiResult {
    const auto local = [&] {
      return realm::InteractionDialog::local(e.point, cs.ui_viewport.drawable_width, cs.ui_viewport.drawable_height);
    };
    if (left_button(e)) {
      if (e.kind == UiKind::pointer_down)
        cs.interaction.pointer_down(local());
      else
        cs.interaction.pointer_up(local());
    } else if (e.kind == UiKind::text)
      cs.interaction.text(e.text);
    else if (e.kind == UiKind::key_down) {
      if (e.key == SDLK_ESCAPE)
        cs.interaction.cancel();
      else if (e.key == SDLK_BACKSPACE)
        cs.interaction.backspace();
      else if (!e.repeat && (e.key == SDLK_RETURN || e.key == SDLK_KP_ENTER))
        cs.interaction.submit();
    } else if (e.kind == UiKind::focus_lost)
      cs.interaction.focus_lost();
    return UiResult::consumed;
  };
}

void configure_board_layer(ClientState& cs, ClientLayers& layers) {
  auto& board_layer = layers.board_layer;
  board_layer.is_active = [&] { return cs.board.visible(); };
  board_layer.is_modal = [] { return true; };
  board_layer.is_text = [&] { return cs.board.wants_text(); };
  board_layer.on_reset = [&](UiReset) { cs.board.close(); };
  board_layer.on_event = [&](const UiEvent& e) -> UiResult {
    switch (e.kind) {
      case UiKind::pointer_move:
        cs.board.pointer_move(e.point);
        break;
      case UiKind::pointer_down:
        if (e.button == SDL_BUTTON_LEFT) cs.board.pointer_down(e.point, (e.modifiers & KMOD_SHIFT) != 0);
        break;
      case UiKind::pointer_up:
        if (e.button == SDL_BUTTON_LEFT) cs.board.pointer_up(e.point);
        break;
      case UiKind::wheel:
        cs.board.wheel(e.wheel);
        break;
      case UiKind::text:
        cs.board.insert(e.text);
        break;
      case UiKind::key_down: {
        const auto k = static_cast<SDL_Keycode>(e.key);
        const bool shift = (e.modifiers & KMOD_SHIFT) != 0, ctrl = (e.modifiers & KMOD_CTRL) != 0;
        using K = realm::GuildBoard::Key;
        std::optional<K> key;
        if (k == SDLK_LEFT)
          key = K::left;
        else if (k == SDLK_RIGHT)
          key = K::right;
        else if (k == SDLK_UP)
          key = K::up;
        else if (k == SDLK_DOWN)
          key = K::down;
        else if (k == SDLK_HOME)
          key = K::home;
        else if (k == SDLK_END)
          key = K::end;
        else if (k == SDLK_BACKSPACE)
          key = K::backspace;
        else if (k == SDLK_DELETE)
          key = K::del;
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
          key = K::enter;
        else if (ctrl && k == SDLK_a)
          key = K::select_all;
        if (key) cs.board.key(*key, shift, ctrl);
        if (ctrl && cs.board.wants_text()) {
          if (k == SDLK_c || k == SDLK_x) {
            const auto selected_text = cs.board.selection();
            if (!selected_text.empty()) SDL_SetClipboardText(selected_text.c_str());
            if (k == SDLK_x) cs.board.cut();
          }
          if (k == SDLK_v) {
            char* pasted = SDL_GetClipboardText();
            if (pasted) {
              cs.board.insert(pasted);
              SDL_free(pasted);
            }
          }
        }
        break;
      }
      default:
        if (window_left(e)) cs.board.focus_lost();
        cs.resized_panel(e, cs.board.visible(), [&](int w, int h) {
          if (cs.board.update(cs.fonts, w, h)) cs.board_texture.update(cs.board.image());
        });
        break;
    }
    if (auto link = cs.board.take_link()) SDL_OpenURL(link->c_str());
    return UiResult::consumed;
  };
}

void configure_members_layer(ClientState& cs, ClientLayers& layers) {
  auto& members_layer = layers.members_layer;
  members_layer.is_active = [&] { return cs.members.visible(); };
  members_layer.is_modal = [] { return true; };
  members_layer.on_reset = [&](UiReset) { cs.members.close(); };
  members_layer.on_event = [&](const UiEvent& e) -> UiResult {
    if (e.kind == UiKind::pointer_move)
      cs.members.pointer_move(e.point);
    else if (left_button(e)) {
      if (e.kind == UiKind::pointer_down)
        cs.members.pointer_down(e.point);
      else
        cs.members.pointer_up(e.point);
    } else if (e.kind == UiKind::wheel)
      cs.members.wheel(e.wheel);
    else if (window_left(e))
      cs.members.focus_lost();
    cs.resized_panel(e, cs.members.visible(), [&](int w, int h) {
      if (cs.members.update(cs.fonts, w, h)) cs.members_texture.update(cs.members.image());
    });
    return UiResult::consumed;
  };
}

}  // namespace realm::application
