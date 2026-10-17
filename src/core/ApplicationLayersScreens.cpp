#include "ApplicationLayers.hpp"

namespace realm::application {

// CreditDisplay's Nexus fame button (_fameButton): hover tints it, a click opens
// FameContentPopup for the current character.
void configure_fame_button_layer(ClientState& cs, ClientLayers& layers) {
  auto& layer = layers.fame_button_layer;
  const auto inside = [&](realm::Vec2 p) {
    const auto& r = cs.fame_button_rect;
    return cs.fame_button_shown && r.width > 0 && p.x >= r.x && p.x < r.x + r.width && p.y >= r.y &&
           p.y < r.y + r.height;
  };
  layer.is_active = [&] { return cs.fame_popup && cs.fame_button_shown && cs.game_session; };
  layer.is_covering = inside;
  layer.on_cancel = [&] { cs.fame_button_hover = false; };
  layer.on_reset = [&](UiReset) { cs.fame_button_hover = false; };
  layer.on_event = [&, inside](const UiEvent& e) -> UiResult {
    if (window_left(e)) {
      cs.fame_button_hover = false;
      return UiResult::pass;
    }
    if (e.kind == UiKind::pointer_move) {
      cs.fame_button_hover = inside(e.point);
      return UiResult::pass;
    }
    if (!left_button(e) || !inside(e.point)) {
      return UiResult::pass;
    }
    if (e.kind == UiKind::pointer_down) {
      return UiResult::capture;
    }
    cs.fame_button_hover = false;
    cs.fame_popup->open(cs.app_host, cs.o.app_port, cs.game_session->credentials(),
                        cs.game_session->character_id());
    return UiResult::consumed;
  };
}

// FameContentPopup is modal: the RmlUi host takes its pointer and wheel events, Escape closes it.
void configure_fame_layer(ClientState& cs, ClientLayers& layers) {
  auto& layer = layers.fame_layer;
  layer.is_active = [&] { return cs.fame_popup && cs.fame_popup->visible(); };
  layer.is_modal = [] { return true; };
  layer.on_cancel = [&] {
    if (cs.production_ui) {
      cs.production_ui->host().layer().cancel();
    }
  };
  layer.on_reset = [&](UiReset) {
    if (cs.fame_popup) {
      cs.fame_popup->close();
    }
  };
  layer.on_event = [&](const UiEvent& e) -> UiResult {
    if (e.kind == UiKind::key_down) {
      if (!e.repeat &&
          realm::sdl_keys::flash_code(static_cast<SDL_Keycode>(e.key)) == realm::key_escape) {
        cs.fame_popup->close();
      }
    } else if (e.kind != UiKind::key_up && e.kind != UiKind::text &&
               e.kind != UiKind::composition && e.kind != UiKind::resized && cs.production_ui) {
      cs.production_ui->host().layer().handle(e);
    }
    return UiResult::consumed;
  };
}

void configure_skins_layer(ClientState& cs, ClientLayers& layers) {
  auto& skins_layer = layers.skins_layer;
  skins_layer.is_active = [&] { return cs.skins && cs.skins->visible(); };
  skins_layer.is_modal = [] { return true; };
  skins_layer.on_reset = [&](UiReset) {
    if (cs.skins) {
      cs.skins->close();
    }
  };
  skins_layer.on_event = [&](const UiEvent& e) -> UiResult {
    if (!cs.skins) {
      return UiResult::pass;
    }
    if (e.kind == UiKind::pointer_move) {
      cs.skins->pointer_move(e.point);
    } else if (left_button(e)) {
      // A press that closes the chooser must not fire once it is gone: the guard holds until the
      // release.
      if (e.kind == UiKind::pointer_down) {
        cs.skin_press_guard = true;
        cs.skins->pointer_down(e.point);
      } else {
        cs.skins->pointer_up(e.point);
      }
    } else if (e.kind == UiKind::wheel) {
      cs.skins->wheel(e.wheel);
    } else if (window_left(e)) {
      cs.skins->focus_lost();
    }

    cs.resized_panel(e, cs.skins->visible(), [&](int w, int h) {
      if (cs.skins->update(w, h, cs.ui_time)) {
        cs.skin_texture.update(cs.skins->image());
      }
    });
    return UiResult::consumed;
  };
}

void configure_options_layer(ClientState& cs, ClientLayers& layers) {
  auto& options_layer = layers.options_layer;
  options_layer.is_active = [&] { return cs.options_menu.visible(); };
  options_layer.is_modal = [] { return true; };
  options_layer.is_text = [&] { return cs.options_screen && cs.options_screen->wants_text(); };
  options_layer.on_cancel = [&] {
    if (cs.production_ui) {
      cs.production_ui->host().layer().cancel();
    }
  };
  options_layer.on_reset = [&](UiReset) { cs.options_menu.close(); };
  options_layer.on_event = [&](const UiEvent& e) -> UiResult {
    auto* host = cs.production_ui ? &cs.production_ui->host().layer() : nullptr;
    const bool typing = cs.options_screen && cs.options_screen->wants_text();
    const bool capturing = cs.options_menu.capturing();
    if (e.kind == UiKind::key_down) {
      const int code = realm::sdl_keys::flash_code(static_cast<SDL_Keycode>(e.key));
      // Auto-repeat would act on the key that has just been bound; the search field still
      // repeats its editing keys.
      if (capturing || !typing) {
        if (!e.repeat) {
          cs.options_menu.key_down(code);
        }
      } else if (code == realm::key_escape) {
        if (!e.repeat) {
          cs.options_screen->leave_search();
        }
      } else if (host) {
        host->handle(e);
      }
    } else if (e.kind == UiKind::key_up || e.kind == UiKind::text ||
               e.kind == UiKind::composition) {
      if (typing && !capturing && host) {
        host->handle(e);
      }
    } else if (host && e.kind != UiKind::resized) {
      host->handle(e);
    }

    if (cs.options_menu.take_changed()) {
      cs.apply_settings();
      cs.options_unsaved = true;
    }
    if (cs.options_unsaved && (e.kind != UiKind::pointer_move || !cs.options_menu.visible())) {
      cs.save_settings();
      cs.options_unsaved = false;
    }

    if (cs.options_menu.take_home() && cs.startup && cs.game_session && !cs.leaving_game) {
      cs.leaving_game = true;
      cs.game_session->disconnect();
    }
    return UiResult::consumed;
  };
}

// Dialog has click events only. Keyboard, wheel and all mouse buttons are consumed by the modal
// layer.
void configure_dialog_layer(ClientState& cs, ClientLayers& layers) {
  auto& dialog_layer = layers.dialog_layer;
  dialog_layer.is_active = [&] { return cs.dialog.visible(); };
  dialog_layer.is_modal = [] { return true; };
  dialog_layer.on_reset = [&](UiReset) { cs.dialog.close(); };
  dialog_layer.on_event = [&](const UiEvent& e) -> UiResult {
    if (e.kind == UiKind::pointer_move) {
      cs.dialog.pointer_move(e.point);
    } else if (left_button(e)) {
      if (e.kind == UiKind::pointer_down) {
        cs.dialog.pointer_down(e.point);
      } else {
        (void)cs.dialog.pointer_up(e.point);
      }
    } else if (window_left(e)) {
      cs.dialog.focus_lost();
    }

    cs.resized_panel(e, cs.dialog.visible(), [&](int w, int h) {
      if (cs.dialog.update(cs.fonts, w, h)) {
        cs.dialog_texture.update(cs.dialog.image());
      }
    });
    return UiResult::consumed;
  };
}

// Startup screens (loading, title, account, characters, classes, skins) fill the window until the
// game starts.
void configure_startup_layer(ClientState& cs, ClientLayers& layers) {
  auto& startup_layer = layers.startup_layer;
  startup_layer.is_active = [&] { return cs.startup && cs.startup->menus(); };
  startup_layer.is_modal = [] { return true; };
  startup_layer.is_text = [&] { return cs.startup_rml->wants_text(); };
  startup_layer.on_cancel = [&] {
    if (cs.startup) {
      cs.startup->cancel_pointer();
    }
    if (cs.production_ui) {
      cs.production_ui->host().layer().cancel();
    }
  };
  // Resize reaches every layer, active or not: a layer whose screen does not exist in this run
  // ignores it.
  startup_layer.on_event = [&](const UiEvent& e) -> UiResult {
    if (!cs.startup) {
      return UiResult::pass;
    }
    if (cs.startup_rml) {
      if (e.kind == UiKind::resized && e.viewport.drawable_height > 0) {
        cs.fonts.set_pixel_scale(e.viewport.drawable_height / 600.0);
        cs.startup->update(cs.ui_time, 0, e.viewport.drawable_width, e.viewport.drawable_height);
        cs.production_ui->host().resize(e.viewport);
        cs.startup_rml->update();
        cs.production_ui->host().update(cs.ui_time);
      }
      const auto result = cs.startup_rml->handle(e);
      if (e.kind == UiKind::key_down && e.key == SDLK_ESCAPE && !e.repeat &&
          result == UiResult::pass) {
        cs.running = false;
      }
      return result;
    }
    return UiResult::consumed;
  };
}

}  // namespace realm::application
