#include "ApplicationLayers.hpp"

namespace realm::application {

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
  options_layer.is_active = [&] { return cs.options_screen.visible(); };
  options_layer.is_modal = [] { return true; };
  options_layer.on_reset = [&](UiReset) { cs.options_screen.close(); };
  options_layer.on_event = [&](const UiEvent& e) -> UiResult {
    if (e.kind == UiKind::pointer_move) {
      cs.options_screen.pointer_move(e.point);
    } else if (left_button(e)) {
      if (e.kind == UiKind::pointer_down) {
        cs.options_screen.pointer_down(e.point);
      } else {
        cs.options_screen.pointer_up(e.point);
      }
    }
    // Auto-repeat would act on the key that has just been bound.
    else if (e.kind == UiKind::key_down) {
      if (!e.repeat) {
        cs.options_screen.key_down(realm::sdl_keys::flash_code(static_cast<SDL_Keycode>(e.key)));
      }
    } else if (window_left(e)) {
      cs.options_screen.focus_lost();
    }

    cs.resized_panel(e, cs.options_screen.visible(), [&](int w, int h) {
      if (cs.options_screen.update(cs.fonts, w, h, cs.ui_time)) {
        cs.options_texture.update(cs.options_screen.image());
      }
    });
    if (cs.options_screen.take_changed()) {
      cs.apply_settings();
      cs.save_settings();
    }

    if (cs.options_screen.take_home() && cs.startup && cs.game_session && !cs.leaving_game) {
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
