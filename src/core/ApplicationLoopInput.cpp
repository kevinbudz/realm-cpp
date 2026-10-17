#include "ApplicationLoop.hpp"
#include "ApplicationLoopDetail.hpp"
#include "realm/util/Telemetry.hpp"
#include "realm/messaging/Session.hpp"
#include "realm/objects/Movement.hpp"
#include "realm/hud/Hud.hpp"
#include "realm/lib/ui/Stack.hpp"
#include "realm/platform/SdlKeyCodes.hpp"
#include "realm/parameters/Settings.hpp"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

#include "ApplicationOptions.hpp"
#include "ApplicationState.hpp"
#include "ApplicationLayers.hpp"

namespace realm::application {
using detail::Clock;
using detail::milliseconds;

namespace {
double mouse_wheel_steps(const SDL_MouseWheelEvent& event) {
  if (event.y) {
    return event.y;
  }
#if SDL_VERSION_ATLEAST(2, 0, 18)
  // sdl2-compat converts pushed wheel events back with preciseY populated
  // and integer y zero. Physical fine scrolling can also have no integer step.
  return event.preciseY;
#else
  return 0;
#endif
}
}

// SDL events in the stack's terms: pointer positions in drawable pixels, wheel in SDL steps
// (positive up).
std::optional<UiEvent> to_ui_event(const ClientState& cs, const SDL_Event& event) {
  UiEvent result;
  const auto pointer = [&](int x, int y) {
    result.point = cs.ui_viewport.drawable(x, y);
    result.modifiers = static_cast<std::uint16_t>(SDL_GetModState());
  };
  switch (event.type) {
    case SDL_MOUSEMOTION:
      result.kind = UiKind::pointer_move;
      pointer(event.motion.x, event.motion.y);
      return result;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      result.kind = event.type == SDL_MOUSEBUTTONDOWN ? UiKind::pointer_down : UiKind::pointer_up;
      result.button = event.button.button;
      pointer(event.button.x, event.button.y);
      return result;
    case SDL_MOUSEWHEEL: {
      result.kind = UiKind::wheel;
      result.wheel = mouse_wheel_steps(event.wheel) *
                     (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
      int x{};
      int y{};
      SDL_GetMouseState(&x, &y);
      pointer(x, y);
      return result;
    }
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      result.kind = event.type == SDL_KEYDOWN ? UiKind::key_down : UiKind::key_up;
      result.key = event.key.keysym.sym;
      result.modifiers = event.key.keysym.mod;
      result.repeat = event.key.repeat != 0;
      return result;
    case SDL_TEXTINPUT:
      result.kind = UiKind::text;
      result.text = event.text.text;
      return result;
    case SDL_TEXTEDITING:
      result.kind = UiKind::composition;
      result.text = event.edit.text;
      result.cursor = event.edit.start;
      return result;
    case SDL_WINDOWEVENT:
      if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        result.kind = UiKind::resized;
        result.viewport = cs.ui_viewport;
        return result;
      }
      // Focus loss neither pauses nor stops sound, so voices run to their end.
      if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        result.kind = UiKind::focus_lost;
        return result;
      }
      if (event.window.event == SDL_WINDOWEVENT_LEAVE) {
        result.kind = UiKind::pointer_left;
        return result;
      }
      return std::nullopt;
    default:
      return std::nullopt;
  }
}

void drive_stress_fixture(ClientState& cs, Frame& f) {
  auto& time = f.time;
  if (cs.stress) {
    cs.stress->step();
    cs.position = cs.stress->camera_position();
    cs.angle = cs.stress->camera_angle();
    cs.zoom = cs.stress->zoom_factor();
    const auto& workload = cs.stress->config().workload;
    if (workload == "UI" || workload == "MIXED") {
      REALM_ZONE("fixture.ui_input");
      const auto phase = cs.frame % 100;
      if (phase == 0) {
        cs.chat.add("Fixture", "Crowded combat and chat scroll", time);
        cs.chat.open();
      }
      if (phase < 20) {
        cs.chat.text("x");
      }
      if (phase == 20) {
        (void)cs.chat.submit();
        cs.chat.scroll_up();
      }

      if (phase == 25) {
        cs.chat.scroll_down();
      }
      if (phase == 40) {
        cs.options_menu.open(cs.settings);
      }
      if (phase == 45) {
        cs.options_menu.set_category(realm::OptionsCategory::video);
      }
      if (phase == 60) {
        cs.options_menu.close();
      }

      if (phase == 70) {
        (void)cs.dialog.purchase_result({1, "Fixture error"});
      }
      if (phase == 80) {
        cs.dialog.close();
      }
      cs.tooltip_pointer_valid = phase >= 85;
      cs.hud_input.move({20, 320}, cs.hud_input.selected());
      if (const auto* player = cs.world.entity(cs.world.player_id())) {
        const auto inventory = realm::HudState::player(cs.world, *player).inventory;
        if (phase == 85) {
          cs.hud_input.down({20, 320}, inventory);
        }
        if (phase == 86) {
          cs.hud_input.move({26, 320}, cs.hud_input.selected());
          if (cs.hud_input.dragging() != 0) {
            throw std::runtime_error("Stress UI failed to drag equipped weapon");
          }
        }
        if (phase == 88) {
          cs.hud_input.move({70, 430}, cs.hud_input.selected());
        }
        if (phase == 90) {
          (void)cs.hud_input.up({70, 430}, false, false, time, inventory);
        }
      }
    }
  }
}

void sync_text_input(ClientState& cs) {
  if (cs.startup || cs.hud) {
    const bool text = cs.ui_stack.wants_text();
    if (text != (SDL_IsTextInputActive() == SDL_TRUE)) {
      if (text) {
        SDL_StartTextInput();
      } else {
        SDL_StopTextInput();
      }
    }
  }
}

void run_script_steps(ClientState& cs, Frame& f) {
  auto& time = f.time;
  auto& script_capture = f.script_capture;
  for (; cs.script_next < cs.script.size() && cs.script[cs.script_next].time <= time;
       ++cs.script_next) {
    const auto& step = cs.script[cs.script_next];
    SDL_Event event{};

    if (step.action == "move" || step.action == "click" || step.action == "down" ||
        step.action == "up") {
      cs.script_pointer = {static_cast<double>(step.x), static_cast<double>(step.y)};
    }
    if (step.action == "down") {
      cs.script_mouse_down = true;
    }
    if (step.action == "up" || step.action == "click") {
      cs.script_mouse_down = false;
    }

    if (step.action == "move") {
      event.type = SDL_MOUSEMOTION;
      event.motion.x = step.x;
      event.motion.y = step.y;
      SDL_PushEvent(&event);
    } else if (step.action == "click") {
      event.type = SDL_MOUSEMOTION;
      event.motion.x = step.x;
      event.motion.y = step.y;
      SDL_PushEvent(&event);
      event = {};
      event.type = SDL_MOUSEBUTTONDOWN;
      event.button.button = SDL_BUTTON_LEFT;
      event.button.state = SDL_PRESSED;
      event.button.x = step.x;
      event.button.y = step.y;
      SDL_PushEvent(&event);
      event.type = SDL_MOUSEBUTTONUP;
      event.button.state = SDL_RELEASED;
      SDL_PushEvent(&event);
    } else if (step.action == "down" || step.action == "up") {
      event.type = step.action == "down" ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
      event.button.button = SDL_BUTTON_LEFT;
      event.button.state = step.action == "down" ? SDL_PRESSED : SDL_RELEASED;
      event.button.x = step.x;
      event.button.y = step.y;
      SDL_PushEvent(&event);
    } else if (step.action == "wheel") {
      event.type = SDL_MOUSEWHEEL;
      event.wheel.y = step.y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
      event.wheel.preciseY = static_cast<float>(step.y);
#endif
      SDL_PushEvent(&event);
    } else if (step.action == "ability-down" || step.action == "ability-up") {
      cs.script_ability_down = step.action == "ability-down";
      event.type = cs.script_ability_down ? SDL_KEYDOWN : SDL_KEYUP;
      event.key.keysym.sym = SDLK_SPACE;
      event.key.keysym.scancode = SDL_SCANCODE_SPACE;
      SDL_PushEvent(&event);
    } else if (step.action == "key") {
      const SDL_Keycode key = SDL_GetKeyFromName(step.argument.c_str());
      if (key == SDLK_UNKNOWN) {
        throw std::invalid_argument("Unknown script key: " + step.argument);
      }
      event.type = SDL_KEYDOWN;
      event.key.state = SDL_PRESSED;
      event.key.keysym.sym = key;
      event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
      SDL_PushEvent(&event);
      // Chat keys act on release, as TextBox's KEY_UP listeners.
      event.type = SDL_KEYUP;
      event.key.state = SDL_RELEASED;
      SDL_PushEvent(&event);
    } else if (step.action == "hold" || step.action == "release") {
      // Polled controls read SDL's keyboard state, which pushed events cannot change.
      const SDL_Keycode key = SDL_GetKeyFromName(step.argument.c_str());
      if (key == SDLK_UNKNOWN) {
        throw std::invalid_argument("Unknown script key: " + step.argument);
      }
      if (step.action == "hold") {
        cs.script_keys.insert(SDL_GetScancodeFromKey(key));
      } else {
        cs.script_keys.erase(SDL_GetScancodeFromKey(key));
      }

    } else if (step.action == "text") {
      // sdl2-compat crashes converting pushed SDL_TEXTINPUT events; the text goes straight to the
      // stack.
      realm::ui::Event typed;
      typed.kind = realm::ui::EventKind::text;
      typed.text = step.argument;
      cs.ui_stack.dispatch(typed);
    } else if (step.action == "compose") {
      realm::ui::Event composed;
      composed.kind = realm::ui::EventKind::composition;
      composed.text = step.argument;
      cs.ui_stack.dispatch(composed);
    } else if (step.action == "focus-lost") {
      event.type = SDL_WINDOWEVENT;
      event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
      SDL_PushEvent(&event);
      cs.script_mouse_down = false;
    } else if (step.action == "resize") {
      if (step.x < 1 || step.y < 1) {
        throw std::invalid_argument("Invalid scripted window size");
      }
      SDL_SetWindowSize(cs.session.window, step.x, step.y);
    } else if (step.action == "capture") {
      script_capture = step.argument;
    } else {
      cs.running = false;
    }
  }
}

void dispatch_events(ClientState& cs, const Options& options, Frame& f) {
  auto& start = f.start;
  auto& input_age = f.input_age;
  auto& input_observed = f.input_observed;
  auto& time = f.time;
  {
    REALM_ZONE("events.poll_dispatch");
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (cs.telemetry &&
          (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP || event.type == SDL_MOUSEMOTION ||
           event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
           event.type == SDL_TEXTINPUT || event.type == SDL_MOUSEWHEEL)) {
        input_observed = true;
        const auto age = static_cast<std::uint32_t>(SDL_GetTicks() - event.common.timestamp);
        // SDL timestamps use its monotonic millisecond tick clock. Synthetic
        // zero/invalid timestamps are observation-to-submit samples.
        input_age = std::max(
            input_age, (event.common.timestamp && age < 60000 ? static_cast<double>(age) : 0) -
                           milliseconds(Clock::now() - start));
      }

      if (event.type == SDL_QUIT) {
        cs.running = false;
      }
      if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
        cs.skin_press_guard = false;
      }
      if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED &&
          !cs.ui_stack.modal()) {
        cs.trade.activate(time);
      }

      cs.ui_viewport = cs.window_viewport();
      const auto converted = to_ui_event(cs, event);
      if (!converted) {
        continue;
      }
      if (cs.game_session && (converted->kind == realm::ui::EventKind::pointer_move ||
                              converted->kind == realm::ui::EventKind::key_down)) {
        cs.idle_watcher.reset();
      }
      realm::ui::Dispatch dispatched;
      dispatched = cs.ui_stack.dispatch(*converted);
      if (dispatched.consumed || event.type != SDL_KEYDOWN) {
        continue;
      }

      const auto key = event.key.keysym.sym;
      // MapUserInput.onKeyDown: every action is the key its Options row binds.
      const int code = realm::sdl_keys::flash_code(key);
      const auto bound = [&](std::string_view param) {
        return code != 0 && code == cs.settings.get(param);
      };
      const bool fresh = !event.key.repeat;
      if (key == SDLK_ESCAPE) {
        if (cs.startup && cs.game_session) {
          if (!event.key.repeat) {
            cs.leaving_game = true;
            cs.game_session->disconnect();
          }
        } else {
          cs.running = false;
        }
      }
      if (cs.game_session && fresh && (bound("escapeToNexus") || bound("escapeToNexus2"))) {
        cs.game_session->escape();
      }
      if (fresh && bound("resetToDefaultCameraAngle")) {
        cs.angle = cs.settings.default_camera_angle();
        cs.save_settings();
      }
      if (bound("options") && fresh && cs.hud && options.world) {
        cs.open_options();
      }
      if (bound("switchTabs") && cs.hud_art && cs.hud && options.world && fresh) {
        if (const auto* player = cs.world.entity(cs.world.player_id())) {
          cs.hud_input.cycle_tabs(realm::HudState::player(cs.world, *player).inventory.backpack);
        }
      }
      static constexpr std::array<std::string_view, 8> inventory_keys{
          "useEquipInvSlot1", "useEquipInvSlot2", "useEquipInvSlot3", "useEquipInvSlot4",
          "useEquipInvSlot5", "useEquipInvSlot6", "useEquipInvSlot7", "useEquipInvSlot8"};
      if (cs.hud_art && options.world && fresh && cs.game_session) {
        for (std::size_t slot = 0; slot < inventory_keys.size(); ++slot) {
          if (bound(inventory_keys[slot])) {
            if (const auto* player = cs.world.entity(cs.world.player_id())) {
              // MapUserInput.useItem: a matching equipment slot takes the item, else it is used.
              cs.game_session->equip_or_use(
                  {player->id,
                   static_cast<std::uint8_t>((cs.hud_input.selected() == 2 ? 12 : 4) + slot)},
                  time);
            }
          }
        }
      }

      // MapUserInput handles every KEY_DOWN, including auto-repeat.
      if (cs.game_session && bound("useHealthPotion")) {
        cs.game_session->use_potion(false, time);
      }
      if (cs.game_session && bound("useMagicPotion")) {
        cs.game_session->use_potion(true, time);
      }

      // PortalPanel.onKeyDown: the interact key enters even while Full or Locked.
      if (cs.game_session && cs.hud_art && options.world && bound("interact")) {
        if (const auto* player = cs.world.entity(cs.world.player_id())) {
          const auto state = realm::HudState::player(cs.world, *player);
          if (state.portal.id >= 0) {
            cs.game_session->use_portal(state.portal.id);
          } else if (state.social.kind == realm::HudState::SocialKind::hall &&
                     state.social.enabled) {
            cs.game_session->use_portal(state.social.id);
          }
        }
      }

      if (fresh && bound("autofireToggle")) {
        cs.autofire = !cs.autofire;
      }
      if (fresh && bound("toggleCentering")) {
        cs.settings.set_value("centerOnPlayer", cs.settings.flag("centerOnPlayer") ? 0 : 1);
        cs.apply_settings();
        cs.save_settings();
      }
      // MapUserInput.togglePerformanceStats flips showStatistics; the overlay follows it.
      if (fresh && bound("togglePerformanceStats")) {
        cs.settings.set_value("showStatistics", cs.settings.flag("showStatistics") ? 0 : 1);
        cs.apply_settings();
        cs.save_settings();
      }
      if (fresh && bound("toggleFullscreenMode") && !options.hidden) {
        const bool fullscreen = (SDL_GetWindowFlags(cs.session.window) &
                                 (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
        SDL_SetWindowFullscreen(cs.session.window, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
      }
      // C toggles centering outside the startup flow (the test-only modes).
      if (key == SDLK_c && !cs.startup) {
        cs.centered = !cs.centered;
      }
      if (options.world && cs.minimap) {
        if (bound("miniMapZoomIn")) {
          cs.minimap->zoom_in();
        }
        if (bound("miniMapZoomOut")) {
          cs.minimap->zoom_out();
        }
      }
    }
  }
}

void after_dispatch(ClientState& cs) {
  cs.ui_stack.update();

  if (auto request = cs.interaction.take_request();
      request && cs.game_session && cs.game_session->state() == realm::SessionState::playing) {
    using K = realm::HudState::SocialKind;
    if (request->kind == K::create_guild) {
      cs.game_session->create_guild(request->value);
    } else if (request->kind == K::leave_guild) {
      cs.game_session->leave_guild(request->value);
    } else {
      cs.game_session->choose_name(request->value);
    }
  }

  if (cs.skins) {
    if (auto selected_skin = cs.skins->take_selection();
        selected_skin && *selected_skin >= 0 && cs.game_session) {
      (void)cs.game_session->reskin(*selected_skin, cs.skins->data().owned_skins);
    }
  }
}

void apply_input(ClientState& cs, const Options& options, Frame& f) {
  auto& dt = f.dt;
  auto& time = f.time;
  const auto keys = SDL_GetKeyboardState(nullptr);
  // A control is down while the key its Options row binds is held (or a script holds it).
  const auto down = [&](std::string_view param) {
    const int code = cs.settings.get(param);
    if (code == 0) {
      return false;
    }
    if (realm::sdl_keys::held(keys, code)) {
      return true;
    }
    const auto key = realm::sdl_keys::sdl_code(code);
    return key != SDLK_UNKNOWN && cs.script_keys.contains(SDL_GetScancodeFromKey(key));
  };

  if (options.world && !options.benchmark && !cs.ui_stack.modal()) {
    // Typed characters never move or turn the player. The rotate keys act only with Allow Camera
    // Rotation.
    const double held = cs.ui_stack.wants_text() ? 0 : 1;
    const bool turning = cs.settings.flag("allowRotation");
    const double rotation = held * (static_cast<double>(turning && down("rotateRight")) -
                                    static_cast<double>(turning && down("rotateLeft")));
    const realm::Vec2 input{
        held * (static_cast<double>(down("moveRight")) - static_cast<double>(down("moveLeft"))),
        held * (static_cast<double>(down("moveDown")) - static_cast<double>(down("moveUp")))};
    if (auto* player = cs.world.entity(cs.world.player_id())) {
      realm::move_player(cs.world, *player, input, rotation, dt * 1000, cs.angle);
      cs.position = player->motion.position;
      if (!cs.game_session) {
        realm::player_move_ack(cs.world, *player);
      }
    } else {
      cs.angle += rotation * dt * 3;
      cs.position.x += (input.x * std::cos(cs.angle) - input.y * std::sin(cs.angle)) * dt * 5;
      cs.position.y += (input.x * std::sin(cs.angle) + input.y * std::cos(cs.angle)) * dt * 5;
    }
  }

  if (cs.game_session) {
    int mouse_x{};
    int mouse_y{};
    auto buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    // SDL_PushEvent does not alter SDL's held-button state. Replay holds
    // explicitly so GPU/network tests exercise the gameplay firing path.
    if (!options.script.empty()) {
      mouse_x = static_cast<int>(cs.script_pointer.x);
      mouse_y = static_cast<int>(cs.script_pointer.y);
      buttons = cs.script_mouse_down ? SDL_BUTTON(SDL_BUTTON_LEFT) : 0;
    }
    const bool ability_held = down("useSpecial") || cs.script_ability_down;
    const bool ability_key = ability_held && !cs.ui_stack.wants_text();
    if (!ability_held) {
      // The key up ends a two-phase ability that the key down started (betterskillys END_USE). It sits
      // outside the input gate below, so a menu or the pointer over the sidebar cannot strand the channel.
      if (cs.special_key.latched()) {
        const auto viewport = cs.window_viewport();
        cs.camera.configure(cs.position, cs.angle, viewport.drawable_width,
                            viewport.drawable_height, cs.zoom, cs.centered);
        auto aim = cs.camera.screen_to_world(viewport.drawable(mouse_x, mouse_y));
        if (!std::isfinite(aim.x) || !std::isfinite(aim.y)) {
          // The server rejects non-finite targets, so fall back to a point ahead of the player.
          aim = {cs.position.x + 3 * std::cos(cs.angle), cs.position.y + 3 * std::sin(cs.angle)};
        }
        cs.game_session->release_ability(aim, time);
      }
      cs.special_key.released();
    }
    if (!cs.ui_stack.modal() &&
        ((buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) || cs.autofire || ability_key)) {
      const auto viewport = cs.window_viewport();
      const auto at = viewport.drawable(mouse_x, mouse_y);
      // The stack's gate: no modal, no press held by a control, nothing drawn under the pointer
      // (the sidebar included).
      if (!cs.skin_press_guard && cs.ui_stack.pointer_free(at) && viewport.valid() &&
          viewport.drawable_width > viewport.drawable_height / 3 && mouse_x >= 0 && mouse_y >= 0 &&
          mouse_y < viewport.window_height && at.x < viewport.world_right()) {
        cs.camera.configure(cs.position, cs.angle, viewport.drawable_width,
                            viewport.drawable_height, cs.zoom, cs.centered);
        const auto target = cs.camera.screen_to_world(at);
        if ((buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) || cs.autofire) {
          cs.game_session->shoot(std::atan2(target.y - cs.position.y, target.x - cs.position.x),
                                 time);
        }
        if (ability_key && cs.special_key.attempts()) {
          cs.special_key.record(cs.game_session->use_ability(target, time));
        }
      }
    }
    cs.game_session->finish_frame(time);
  }

  if (!cs.game_session && !cs.stress) {
    cs.world.particles().update(cs.world, time);
  }
  // This frame's sounds, after input and the session update: game events, menu clicks and the death
  // screen.
  cs.audio_bridge.play(cs.world.sounds());
  if (cs.startup) {
    cs.audio_bridge.play(cs.startup->sound_cues());
  }
  cs.audio_bridge.tick();
}

}  // namespace realm::application
