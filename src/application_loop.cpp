#include "application_loop.hpp"
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

namespace realm::application {
namespace {
using Clock = std::chrono::steady_clock;
double mouse_wheel_steps(const SDL_MouseWheelEvent& event) {
  if (event.y) return event.y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
  // sdl2-compat converts pushed wheel events back with preciseY populated
  // and integer y zero. Physical fine scrolling can also have no integer step.
  return event.preciseY;
#else
  return 0;
#endif
}
double milliseconds(Clock::duration duration) { return std::chrono::duration<double, std::milli>(duration).count(); }
void report(std::string_view name, std::vector<double> values) {
  if (values.empty()) return;
  std::sort(values.begin(), values.end());
  const double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
  const auto p95 = static_cast<std::size_t>(std::ceil(static_cast<double>(values.size()) * .95)) - 1;
  std::cout << name << " mean=" << mean << "ms p95=" << values[p95] << "ms samples=" << values.size() << '\n';
}
}

// SDL events in the stack's terms: pointer positions in drawable pixels, wheel in SDL steps (positive up).
std::optional<UiEvent> to_ui_event(const ClientState& cs, const SDL_Event& event) {
  UiEvent e;
  const auto pointer = [&](int x, int y) {
    e.point = cs.ui_viewport.drawable(x, y);
    e.modifiers = static_cast<std::uint16_t>(SDL_GetModState());
  };
  switch (event.type) {
    case SDL_MOUSEMOTION:
      e.kind = UiKind::pointer_move;
      pointer(event.motion.x, event.motion.y);
      return e;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      e.kind = event.type == SDL_MOUSEBUTTONDOWN ? UiKind::pointer_down : UiKind::pointer_up;
      e.button = event.button.button;
      pointer(event.button.x, event.button.y);
      return e;
    case SDL_MOUSEWHEEL: {
      e.kind = UiKind::wheel;
      e.wheel = mouse_wheel_steps(event.wheel) * (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
      int x{}, y{};
      SDL_GetMouseState(&x, &y);
      pointer(x, y);
      return e;
    }
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      e.kind = event.type == SDL_KEYDOWN ? UiKind::key_down : UiKind::key_up;
      e.key = event.key.keysym.sym;
      e.modifiers = event.key.keysym.mod;
      e.repeat = event.key.repeat != 0;
      return e;
    case SDL_TEXTINPUT:
      e.kind = UiKind::text;
      e.text = event.text.text;
      return e;
    case SDL_TEXTEDITING:
      e.kind = UiKind::composition;
      e.text = event.edit.text;
      e.cursor = event.edit.start;
      return e;
    case SDL_WINDOWEVENT:
      if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        e.kind = UiKind::resized;
        e.viewport = cs.ui_viewport;
        return e;
      }
      // Focus loss neither pauses nor stops sound, so voices run to their end.
      if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        e.kind = UiKind::focus_lost;
        return e;
      }
      if (event.window.event == SDL_WINDOWEVENT_LEAVE) {
        e.kind = UiKind::pointer_left;
        return e;
      }
      return std::nullopt;
    default:
      return std::nullopt;
  }
}

void begin_frame(ClientState& cs, const Options& o, Frame& f) {
  auto& start = f.start;
  auto& frame_interval = f.frame_interval;
  auto& input_age = f.input_age;
  auto& input_observed = f.input_observed;
  auto& previous_drops = f.previous_drops;
  auto& dt = f.dt;
  auto& time = f.time;
  auto& cpu_zone = f.cpu_zone;
  start = Clock::now();
  frame_interval = milliseconds(start - cs.previous);
  if (cs.telemetry) cs.telemetry->begin(cs.frame, start);
  cpu_zone.emplace("frame.cpu");
  input_age = -60000;
  input_observed = false;
  previous_drops = cs.world.particles().dropped();
  dt = cs.stress        ? cs.stress->config().step_ms / 1000.0
       : o.fixture_step ? o.fixture_step / 1000.0
                        : std::min(frame_interval / 1000, .1);
  cs.previous = start;
  time = cs.stress        ? cs.stress->time()
         : o.fixture_step ? static_cast<std::int32_t>(cs.frame * static_cast<std::uint64_t>(o.fixture_step))
                          : static_cast<std::int32_t>(milliseconds(start - cs.epoch));
  cs.ui_time = time;
  cs.ui_viewport = cs.window_viewport();
}

void drive_stress_fixture(ClientState& cs, Frame& f) {
  auto& time = f.time;
  if (cs.stress) {
    cs.stress->step();
    cs.position = cs.stress->camera_position();
    cs.angle = cs.stress->camera_angle();
    cs.zoom = cs.stress->zoom_factor();
    const auto& kind = cs.stress->config().workload;
    if (kind == "UI" || kind == "MIXED") {
      REALM_ZONE("fixture.ui_input");
      const auto phase = cs.frame % 100;
      if (phase == 0) {
        cs.chat.add("Fixture", "Crowded combat and chat scroll", time);
        cs.chat.open();
      }
      if (phase < 20) cs.chat.text("x");
      if (phase == 20) {
        (void)cs.chat.submit();
        cs.chat.scroll_up();
      }
      if (phase == 25) cs.chat.scroll_down();
      if (phase == 40) cs.options_screen.open(cs.settings);
      if (phase == 45) cs.options_screen.set_tab(realm::OptionsTab::graphics);
      if (phase == 60) cs.options_screen.close();
      if (phase == 70) (void)cs.dialog.purchase_result({1, "Fixture error"});
      if (phase == 80) cs.dialog.close();
      cs.tooltip_pointer_valid = phase >= 85;
      cs.hud_input.move({20, 320}, cs.hud_input.selected());
      if (const auto* p = cs.world.entity(cs.world.player_id())) {
        const auto inventory = realm::HudState::player(cs.world, *p).inventory;
        if (phase == 85) cs.hud_input.down({20, 320}, inventory);
        if (phase == 86) {
          cs.hud_input.move({26, 320}, cs.hud_input.selected());
          if (cs.hud_input.dragging() != 0) throw std::runtime_error("Stress UI failed to drag equipped weapon");
        }
        if (phase == 88) cs.hud_input.move({70, 430}, cs.hud_input.selected());
        if (phase == 90) (void)cs.hud_input.up({70, 430}, false, false, time, inventory);
      }
    }
  }
}

void update_session(ClientState& cs, Frame& f) {
  auto& dt = f.dt;
  auto& time = f.time;
  if (cs.terrain) cs.terrain->begin_frame();
  if (cs.startup)
    if (auto launch = cs.startup->take_launch()) {
      cs.game_session = std::make_unique<realm::Session>(cs.world);
      cs.background_world = false;
      try {
        cs.game_session->connect(std::move(*launch));
      } catch (const std::exception& e) {
        cs.game_session.reset();
        cs.startup->left_game(e.what());
        cs.startup->load_background(cs.world);
        cs.background_world = true;
      }
    }
  if (cs.game_session) {
    cs.game_session->poll(time);
    cs.world.update(dt * 1000);
    for (const auto& message : cs.game_session->take_events()) {
      // A new map drops plays still waiting for a load (plays never outlive their map's objects).
      if (message.id == realm::flash::PacketId::MAPINFO) cs.audio_bridge.map_changed();
      if (message.id == realm::flash::PacketId::MAPINFO || message.id == realm::flash::PacketId::RECONNECT ||
          message.id == realm::flash::PacketId::DEATH) {
        cs.ui_stack.reset(realm::ui::Reset::map_change);
      }
      cs.interaction.receive(message);
      if (auto name = cs.interaction.take_name(); name && cs.startup) cs.startup->renamed_account(*name);
      // onBuyResult: result 1 opens a "Purchase Error" dialog; other
      // results become chat lines (server color on success, otherwise *Error*).
      if (message.id == realm::flash::PacketId::BUYRESULT) {
        const auto& result = std::get<realm::net::incoming::BuyResult>(message.data);
        if (cs.dialog.purchase_result(result)) {
          // The dialog is modal: the stack cancels presses, drags and hover beneath it; these menus close.
          cs.chat.close();
          if (cs.player_menu) cs.player_menu->close();
          if (cs.player_group) cs.player_group->close();
          cs.party_arrows.reset();
          cs.ui_stack.update();
        } else
          cs.chat.add(std::string(result.result == 0 ? realm::server_chat_name : realm::error_chat_name), result.text,
                      time);
      }
      switch (message.id) {
        case realm::flash::PacketId::TRADEREQUESTED: {
          const auto& name = std::get<realm::net::incoming::TradeRequested>(message.data).name;
          if (cs.trade_view && !cs.trade.active() && cs.settings.flag("showTradePopup")) cs.trade.requested(name, time);
          cs.chat.add("", name + " wants to trade with you.  Type \"/trade " + name + "\" to trade.", time);
          break;
        }
        case realm::flash::PacketId::TRADESTART:
          cs.trade.start(std::get<realm::net::incoming::TradeStart>(message.data), cs.own_name(), time);
          cs.trade_pressed = -1;
          break;
        case realm::flash::PacketId::TRADECHANGED:
          cs.trade.changed(std::get<realm::net::incoming::TradeChanged>(message.data), time);
          break;
        case realm::flash::PacketId::TRADEACCEPTED:
          cs.trade.accepted(std::get<realm::net::incoming::TradeAccepted>(message.data));
          break;
        case realm::flash::PacketId::TRADEDONE:
          cs.trade.end();
          cs.trade_pressed = -1;
          cs.chat.add("", std::get<realm::net::incoming::TradeDone>(message.data).text, time);
          break;
        default:
          break;
      }
      if (message.id == realm::flash::PacketId::TEXT) {
        const auto& text = std::get<realm::net::incoming::Text>(message.data);
        cs.members.receive(text, cs.world.player_id());
        cs.balloons.add(cs.world, text);
        cs.chat.add(text, cs.own_name(), time);
      }
    }
    const auto state = cs.game_session->state();
    if (cs.startup) {
      if (state == realm::SessionState::playing && cs.startup->menus()) cs.startup->entered_game();
      if (state == realm::SessionState::failed || state == realm::SessionState::disconnected ||
          state == realm::SessionState::dead) {
        const auto death = cs.game_session->death();
        if (death) {
          int snapshot_width{}, snapshot_height{};
          SDL_GL_GetDrawableSize(cs.session.window, &snapshot_width, &snapshot_height);
          cs.death_background = std::make_unique<realm::SurfaceTexture>();
          cs.death_background->update(cs.renderer.capture(snapshot_width, snapshot_height, true));
        }
        std::string message = cs.leaving_game ? "" : cs.game_session->error();
        if (message.empty() && state != realm::SessionState::dead && !cs.leaving_game)
          message = "Disconnected from the game server.";
        // Leaving, disconnecting or dying ends every voice; the death screen's sound is queued after this.
        cs.audio_bridge.stop();
        cs.game_session.reset();
        cs.ui_stack.reset(realm::ui::Reset::session_end);
        cs.leaving_game = false;
        if (death)
          cs.startup->died(*death, time);
        else
          cs.startup->left_game(message);
        cs.startup->load_background(cs.world);
        cs.background_world = true;
      }
    } else if (state == realm::SessionState::failed || state == realm::SessionState::disconnected)
      throw std::runtime_error(cs.game_session->error());
    cs.interaction.update(cs.world);
    if (cs.game_session)
      if (const auto* player = cs.world.entity(cs.world.player_id())) cs.position = player->motion.position;
  }
}

void update_panels(ClientState& cs, Frame& f) {
  auto& dt = f.dt;
  auto& menus = f.menus;
  if (cs.startup)
    if (auto error = cs.startup->take_skin_error())
      cs.dialog.message_close("D'oh, this isn't good", "An error has occured:\n" + *error, "Ok");
  cs.board.poll();
  cs.members.poll(dt);
  if (cs.skins) cs.skins->poll();
  if (cs.members.visible() && cs.game_session)
    if (const auto* player = cs.world.entity(cs.world.player_id()))
      cs.members.permissions(player->profile.guild,
                             player->profile.stats.value_or(realm::flash::StatId::GUILD_RANK_STAT, 0));
  if (auto action = cs.members.take_request(); action && cs.game_session) {
    if (action->action == realm::GuildMembers::Action::remove)
      cs.game_session->leave_guild(action->name);
    else
      cs.game_session->change_guild_rank(action->name, action->rank);
  }
  if (cs.board.visible() && cs.game_session)
    if (const auto* player = cs.world.entity(cs.world.player_id()))
      cs.board.permissions(player->profile.stats.value_or(realm::flash::StatId::GUILD_RANK_STAT, 0));
  menus = cs.startup && cs.startup->menus();
  cs.ui_stack.update();
}

void sync_text_input(ClientState& cs) {
  if (cs.startup || cs.hud) {
    const bool text = cs.ui_stack.wants_text();
    if (text != (SDL_IsTextInputActive() == SDL_TRUE)) {
      if (text)
        SDL_StartTextInput();
      else
        SDL_StopTextInput();
    }
  }
}

void run_script_steps(ClientState& cs, Frame& f) {
  auto& time = f.time;
  auto& script_capture = f.script_capture;
  for (; cs.script_next < cs.script.size() && cs.script[cs.script_next].time <= time; ++cs.script_next) {
    const auto& step = cs.script[cs.script_next];
    SDL_Event injected{};
    if (step.action == "move" || step.action == "click" || step.action == "down" || step.action == "up")
      cs.script_pointer = {static_cast<double>(step.x), static_cast<double>(step.y)};
    if (step.action == "down") cs.script_mouse_down = true;
    if (step.action == "up" || step.action == "click") cs.script_mouse_down = false;
    if (step.action == "move") {
      injected.type = SDL_MOUSEMOTION;
      injected.motion.x = step.x;
      injected.motion.y = step.y;
      SDL_PushEvent(&injected);
    } else if (step.action == "click") {
      injected.type = SDL_MOUSEMOTION;
      injected.motion.x = step.x;
      injected.motion.y = step.y;
      SDL_PushEvent(&injected);
      injected = {};
      injected.type = SDL_MOUSEBUTTONDOWN;
      injected.button.button = SDL_BUTTON_LEFT;
      injected.button.state = SDL_PRESSED;
      injected.button.x = step.x;
      injected.button.y = step.y;
      SDL_PushEvent(&injected);
      injected.type = SDL_MOUSEBUTTONUP;
      injected.button.state = SDL_RELEASED;
      SDL_PushEvent(&injected);
    } else if (step.action == "down" || step.action == "up") {
      injected.type = step.action == "down" ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
      injected.button.button = SDL_BUTTON_LEFT;
      injected.button.state = step.action == "down" ? SDL_PRESSED : SDL_RELEASED;
      injected.button.x = step.x;
      injected.button.y = step.y;
      SDL_PushEvent(&injected);
    } else if (step.action == "wheel") {
      injected.type = SDL_MOUSEWHEEL;
      injected.wheel.y = step.y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
      injected.wheel.preciseY = static_cast<float>(step.y);
#endif
      SDL_PushEvent(&injected);
    } else if (step.action == "ability-down" || step.action == "ability-up") {
      cs.script_ability_down = step.action == "ability-down";
      injected.type = cs.script_ability_down ? SDL_KEYDOWN : SDL_KEYUP;
      injected.key.keysym.sym = SDLK_SPACE;
      injected.key.keysym.scancode = SDL_SCANCODE_SPACE;
      SDL_PushEvent(&injected);
    } else if (step.action == "key") {
      const SDL_Keycode key = SDL_GetKeyFromName(step.argument.c_str());
      if (key == SDLK_UNKNOWN) throw std::invalid_argument("Unknown script key: " + step.argument);
      injected.type = SDL_KEYDOWN;
      injected.key.state = SDL_PRESSED;
      injected.key.keysym.sym = key;
      injected.key.keysym.scancode = SDL_GetScancodeFromKey(key);
      SDL_PushEvent(&injected);
      // Chat keys act on release, as TextBox's KEY_UP listeners.
      injected.type = SDL_KEYUP;
      injected.key.state = SDL_RELEASED;
      SDL_PushEvent(&injected);
    } else if (step.action == "hold" || step.action == "release") {
      // Polled controls read SDL's keyboard state, which pushed events cannot change.
      const SDL_Keycode key = SDL_GetKeyFromName(step.argument.c_str());
      if (key == SDLK_UNKNOWN) throw std::invalid_argument("Unknown script key: " + step.argument);
      if (step.action == "hold")
        cs.script_keys.insert(SDL_GetScancodeFromKey(key));
      else
        cs.script_keys.erase(SDL_GetScancodeFromKey(key));
    } else if (step.action == "text") {
      // sdl2-compat crashes converting pushed SDL_TEXTINPUT events; the text goes straight to the stack.
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
      injected.type = SDL_WINDOWEVENT;
      injected.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
      SDL_PushEvent(&injected);
      cs.script_mouse_down = false;
    } else if (step.action == "resize") {
      if (step.x < 1 || step.y < 1) throw std::invalid_argument("Invalid scripted window size");
      SDL_SetWindowSize(cs.session.window, step.x, step.y);
    } else if (step.action == "capture")
      script_capture = step.argument;
    else
      cs.running = false;
  }
}

void update_panel_pixels(ClientState& cs, Frame& f) {
  auto& dt = f.dt;
  auto& time = f.time;
  if (cs.dialog.visible()) {
    int dw{}, dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.dialog.update(cs.fonts, dw, dh)) cs.dialog_texture.update(cs.dialog.image());
    }
  }
  if (cs.members.visible()) {
    int dw{}, dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.members.update(cs.fonts, dw, dh, dt)) cs.members_texture.update(cs.members.image());
    }
  }
  if (cs.skins && cs.skins->visible()) {
    int dw{}, dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.skins->update(dw, dh, time)) cs.skin_texture.update(cs.skins->image());
    }
  }
  if (cs.options_screen.visible()) {
    int dw{}, dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.options_screen.update(cs.fonts, dw, dh, time)) cs.options_texture.update(cs.options_screen.image());
    }
  }
  if (cs.board.visible()) {
    int dw{}, dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.board.update(cs.fonts, dw, dh, dt)) cs.board_texture.update(cs.board.image());
    }
  }
}

void dispatch_events(ClientState& cs, const Options& o, Frame& f) {
  auto& start = f.start;
  auto& input_age = f.input_age;
  auto& input_observed = f.input_observed;
  auto& time = f.time;
  {
    REALM_ZONE("events.poll_dispatch");
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (cs.telemetry && (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP || event.type == SDL_MOUSEMOTION ||
                           event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
                           event.type == SDL_TEXTINPUT || event.type == SDL_MOUSEWHEEL)) {
        input_observed = true;
        const auto age = static_cast<std::uint32_t>(SDL_GetTicks() - event.common.timestamp);
        // SDL timestamps use its monotonic millisecond tick clock. Synthetic
        // zero/invalid timestamps are observation-to-submit samples.
        input_age = std::max(input_age, (event.common.timestamp && age < 60000 ? static_cast<double>(age) : 0) -
                                            milliseconds(Clock::now() - start));
      }
      if (event.type == SDL_QUIT) cs.running = false;
      if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) cs.skin_press_guard = false;
      if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED && !cs.ui_stack.modal())
        cs.trade.activate(time);
      cs.ui_viewport = cs.window_viewport();
      const auto converted = to_ui_event(cs, event);
      if (!converted) continue;
      realm::ui::Dispatch dispatched;
      dispatched = cs.ui_stack.dispatch(*converted);
      if (dispatched.consumed || event.type != SDL_KEYDOWN) continue;
      const auto key = event.key.keysym.sym;
      // MapUserInput.onKeyDown: every action is the key its Options row binds.
      const int code = realm::sdl_keys::flash_code(key);
      const auto bound = [&](std::string_view param) { return code != 0 && code == cs.settings.get(param); };
      const bool fresh = !event.key.repeat;
      if (key == SDLK_ESCAPE) {
        if (cs.startup && cs.game_session) {
          if (!event.key.repeat) {
            cs.leaving_game = true;
            cs.game_session->disconnect();
          }
        } else
          cs.running = false;
      }
      if (cs.game_session && fresh && (bound("escapeToNexus") || bound("escapeToNexus2"))) cs.game_session->escape();
      if (fresh && bound("resetToDefaultCameraAngle")) {
        cs.angle = cs.settings.default_camera_angle();
        cs.save_settings();
      }
      if (bound("options") && fresh && cs.hud && o.world) cs.open_options();
      if (bound("switchTabs") && cs.hud_art && cs.hud && o.world && fresh)
        if (const auto* player = cs.world.entity(cs.world.player_id()))
          cs.hud_input.cycle_tabs(realm::HudState::player(cs.world, *player).inventory.backpack);
      static constexpr std::array<std::string_view, 8> inventory_keys{
          "useEquipInvSlot1", "useEquipInvSlot2", "useEquipInvSlot3", "useEquipInvSlot4",
          "useEquipInvSlot5", "useEquipInvSlot6", "useEquipInvSlot7", "useEquipInvSlot8"};
      if (cs.hud_art && o.world && fresh && cs.game_session)
        for (std::size_t slot = 0; slot < inventory_keys.size(); ++slot)
          if (bound(inventory_keys[slot]))
            if (const auto* player = cs.world.entity(cs.world.player_id()))
              cs.game_session->use_item(
                  {player->id, static_cast<std::uint8_t>((cs.hud_input.selected() == 2 ? 12 : 4) + slot)}, time);
      // MapUserInput handles every KEY_DOWN, including auto-repeat.
      if (cs.game_session && bound("useHealthPotion")) cs.game_session->use_potion(false, time);
      if (cs.game_session && bound("useMagicPotion")) cs.game_session->use_potion(true, time);
      // PortalPanel.onKeyDown: the interact key enters even while Full or Locked.
      if (cs.game_session && cs.hud_art && o.world && bound("interact"))
        if (const auto* player = cs.world.entity(cs.world.player_id())) {
          const auto state = realm::HudState::player(cs.world, *player);
          if (state.portal.id >= 0)
            cs.game_session->use_portal(state.portal.id);
          else if (state.social.kind == realm::HudState::SocialKind::hall && state.social.enabled)
            cs.game_session->use_portal(state.social.id);
        }
      if (fresh && bound("autofireToggle")) cs.autofire = !cs.autofire;
      if (fresh && bound("toggleCentering")) {
        cs.settings.set_choice("centerOnPlayer", cs.settings.flag("centerOnPlayer") ? 0 : 1);
        cs.apply_settings();
        cs.save_settings();
      }
      if (fresh && bound("togglePerformanceStats")) {
        cs.perf_stats = !cs.perf_stats;
        cs.reset_perf();
      }
      if (fresh && bound("toggleFullscreenMode") && !o.hidden) {
        const bool fullscreen =
            (SDL_GetWindowFlags(cs.session.window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
        SDL_SetWindowFullscreen(cs.session.window, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
      }
      // C toggles centering outside the startup flow (the test-only modes).
      if (key == SDLK_c && !cs.startup) cs.centered = !cs.centered;
      if (o.world && cs.minimap) {
        if (bound("miniMapZoomIn")) cs.minimap->zoom_in();
        if (bound("miniMapZoomOut")) cs.minimap->zoom_out();
      }
    }
  }
}

void after_dispatch(ClientState& cs) {
  cs.ui_stack.update();
  if (auto request = cs.interaction.take_request();
      request && cs.game_session && cs.game_session->state() == realm::SessionState::playing) {
    using K = realm::HudState::SocialKind;
    if (request->kind == K::create_guild)
      cs.game_session->create_guild(request->value);
    else if (request->kind == K::leave_guild)
      cs.game_session->leave_guild(request->value);
    else
      cs.game_session->choose_name(request->value);
  }
  if (cs.skins)
    if (auto selected_skin = cs.skins->take_selection(); selected_skin && *selected_skin >= 0 && cs.game_session) {
      (void)cs.game_session->reskin(*selected_skin, cs.skins->data().owned_skins);
    }
}

void apply_input(ClientState& cs, const Options& o, Frame& f) {
  auto& dt = f.dt;
  auto& time = f.time;
  const auto keys = SDL_GetKeyboardState(nullptr);
  // A control is down while the key its Options row binds is held (or a script holds it).
  const auto down = [&](std::string_view param) {
    const int code = cs.settings.get(param);
    if (code == 0) return false;
    if (realm::sdl_keys::held(keys, code)) return true;
    const auto key = realm::sdl_keys::sdl_code(code);
    return key != SDLK_UNKNOWN && cs.script_keys.contains(SDL_GetScancodeFromKey(key));
  };
  if (o.world && !o.benchmark && !cs.ui_stack.modal()) {
    // Typed characters never move or turn the player. The rotate keys act only with Allow Camera Rotation.
    const double held = cs.ui_stack.wants_text() ? 0 : 1;
    const bool turning = cs.settings.flag("allowRotation");
    const double rotation = held * (static_cast<double>(turning && down("rotateRight")) -
                                    static_cast<double>(turning && down("rotateLeft")));
    const realm::Vec2 input{held * (static_cast<double>(down("moveRight")) - static_cast<double>(down("moveLeft"))),
                            held * (static_cast<double>(down("moveDown")) - static_cast<double>(down("moveUp")))};
    if (auto* player = cs.world.entity(cs.world.player_id())) {
      realm::move_player(cs.world, *player, input, rotation, dt * 1000, cs.angle);
      cs.position = player->motion.position;
      if (!cs.game_session) realm::player_move_ack(cs.world, *player);
    } else {
      cs.angle += rotation * dt * 3;
      cs.position.x += (input.x * std::cos(cs.angle) - input.y * std::sin(cs.angle)) * dt * 5;
      cs.position.y += (input.x * std::sin(cs.angle) + input.y * std::cos(cs.angle)) * dt * 5;
    }
  }
  if (cs.game_session) {
    int mouse_x{}, mouse_y{};
    auto buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    // SDL_PushEvent does not alter SDL's held-button state. Replay holds
    // explicitly so GPU/network tests exercise the gameplay firing path.
    if (!o.script.empty()) {
      mouse_x = static_cast<int>(cs.script_pointer.x);
      mouse_y = static_cast<int>(cs.script_pointer.y);
      buttons = cs.script_mouse_down ? SDL_BUTTON(SDL_BUTTON_LEFT) : 0;
    }
    const bool ability_held = down("useSpecial") || cs.script_ability_down;
    const bool ability_key = ability_held && !cs.ui_stack.wants_text();
    if (!ability_held) cs.special_key.released();
    if (!cs.ui_stack.modal() && ((buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) || cs.autofire || ability_key)) {
      const auto viewport = cs.window_viewport();
      const auto at = viewport.drawable(mouse_x, mouse_y);
      // The stack's gate: no modal, no press held by a control, nothing drawn under the pointer (the sidebar
      // included).
      if (!cs.skin_press_guard && cs.ui_stack.pointer_free(at) && viewport.valid() &&
          viewport.drawable_width > viewport.drawable_height / 3 && mouse_x >= 0 && mouse_y >= 0 &&
          mouse_y < viewport.window_height && at.x < viewport.world_right()) {
        cs.camera.configure(cs.position, cs.angle, viewport.drawable_width, viewport.drawable_height, cs.zoom,
                            cs.centered);
        const auto target = cs.camera.screen_to_world(at);
        if ((buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) || cs.autofire)
          cs.game_session->shoot(std::atan2(target.y - cs.position.y, target.x - cs.position.x), time);
        if (ability_key && cs.special_key.attempts()) cs.special_key.record(cs.game_session->use_ability(target, time));
      }
    }
    cs.game_session->finish_frame(time);
  }
  if (!cs.game_session && !cs.stress) cs.world.particles().update(cs.world, time);
  // This frame's sounds, after input and the session update: game events, menu clicks and the death screen.
  cs.audio_bridge.play(cs.world.sounds());
  if (cs.startup) cs.audio_bridge.play(cs.startup->sound_cues());
  cs.audio_bridge.tick();
}

bool prepare_frame(ClientState& cs, Frame& f) {
  auto& width = f.width;
  auto& height = f.height;
  auto& cpu_zone = f.cpu_zone;
  SDL_GL_GetDrawableSize(cs.session.window, &width, &height);
  if (width <= 0 || height <= 0) {
    cs.reset_perf();
    cpu_zone->finish();
    realm::telemetry::active = nullptr;
    SDL_Delay(10);
    cs.previous_capture = true;
    return false;
  }

  return true;
}

void build_scene(ClientState& cs, const Options& o, Frame& f) {
  auto& dt = f.dt;
  auto& time = f.time;
  auto& width = f.width;
  auto& height = f.height;
  auto& clip_width = f.clip_width;
  cs.fonts.set_pixel_scale(height / 600.0);
  if (cs.startup) {
    REALM_ZONE("ui.startup");
    cs.startup->update(time, dt * 1000, width, height);
  }
  cs.quads.clear();
  clip_width = width;
  if (cs.startup && cs.startup->menus()) {
    if (cs.startup->background() && cs.background_world) {
      // MapBackground centers an at least 800x600 viewport on the stage.
      const double view_w = std::max(800, width), view_h = std::max(600, height);
      const auto at = cs.startup->background_position();
      cs.camera.configure(at, realm::Startup::background_angle, realm::Rect{-view_w / 2, -view_h / 2, view_w, view_h});
      cs.fonts.set_pixel_scale(1);
      cs.scene.build(cs.world, cs.camera, cs.atlas, cs.animations, nullptr, cs.terrain.get(), &cs.appearances, at,
                     realm::Startup::background_angle, 1, width, height, time, cs.quads);
      cs.fonts.set_pixel_scale(height / 600.0);
    }
  } else if (o.world) {
    if (width <= height / 3) throw std::runtime_error("Window too narrow for game camera");
    const auto shake = cs.world.particles().camera_offset(time);
    cs.camera.configure({cs.position.x + shake.x, cs.position.y + shake.y}, cs.angle, width, height, cs.zoom,
                        cs.centered);
    clip_width = static_cast<int>(cs.camera.playable_width());
    if (!o.map.empty() || cs.game_session || cs.stress) {
      // MAPINFO background (stars / nexus water), redone whenever a new map loads.
      if (cs.backdrop_generation != cs.world.generation()) {
        cs.backdrop.reset(cs.game_session ? cs.game_session->map_info().background : 0,
                          cs.game_session ? cs.game_session->map_info().seed : 1U);
        cs.backdrop_generation = cs.world.generation();
      }
      cs.backdrop.build(cs.camera, cs.atlas, time, cs.quads);
      cs.fonts.set_pixel_scale(cs.zoom);
      cs.scene.build(cs.world, cs.camera, cs.atlas, cs.animations, nullptr, cs.terrain.get(), &cs.appearances,
                     cs.position, cs.angle, cs.zoom, clip_width, height, o.scene_time >= 0 ? o.scene_time : time,
                     cs.quads);
      cs.fonts.set_pixel_scale(height / 600.0);
    }
  }
  if (cs.terrain)
    for (const auto slot : cs.terrain->changes())
      cs.atlas.update_additional(cs.terrain_first + slot, cs.terrain->images().at(slot));
}

void build_hud_overlays(ClientState& cs, const Options& o, Frame& f) {
  auto& time = f.time;
  auto& menus = f.menus;
  auto& width = f.width;
  auto& height = f.height;
  auto& ui_zone = f.ui_zone;
  auto& overlay_storage = f.overlay_storage;
  auto& overlay = f.overlay;
  ui_zone.emplace("ui.build");
  if (cs.startup) {
    // Session transitions may change the controller after its simulation update in this frame.
    cs.startup->update(time, 0, width, height);
    cs.startup_rml->update();
  }
  if (cs.hud && o.world && !menus)
    if (const auto* player = cs.world.entity(cs.world.player_id())) {
      const auto* party = cs.game_session ? &cs.game_session->party() : cs.stress ? cs.stress->party() : nullptr;
      auto state = cs.hud_state_cache.player(cs.world, *player, party, time);
      const bool hover_enabled = !cs.dialog.visible() && !cs.board.visible() && !cs.members.visible() &&
                                 !(cs.skins && cs.skins->visible()) && cs.tooltip_pointer_valid && !cs.hud_mouse_down &&
                                 !cs.interaction.visible() && !cs.trade.active() && !cs.trade.request() &&
                                 cs.game_session && cs.game_session->state() == realm::SessionState::playing;
      if (cs.game_session)
        cs.party_hover.update(cs.world, cs.game_session->party(), state, cs.hud_view, cs.hud_input.pointer(),
                              hover_enabled, time);
      else
        cs.party_hover.reset();
      if (cs.player_tooltip) {
        // PartyPanel.onMouseOver shows no tooltip while its menu is open.
        const auto target = hover_enabled && !(cs.player_menu && cs.player_menu->visible()) &&
                                    !(cs.player_group && cs.player_group->visible())
                                ? realm::player_tooltip_target(cs.world, cs.party_hover.target())
                                : std::nullopt;
        if (cs.player_tooltip->update(target)) cs.player_tooltip_texture.update(cs.player_tooltip->image());
      }
      if (!state.inventory.backpack && cs.hud_input.selected() == 2) cs.hud_input.cancel(true);
      if (state.inventory.container != cs.hud_container) {
        cs.hud_input.container_changed();
        cs.hud_container = state.inventory.container;
      }
      state.inventory.selected = cs.hud_input.selected();
      state.inventory.dragging = cs.hud_input.dragging();
      state.retain(cs.hud_view);
      state.sellable.enabled = !cs.game_session || !cs.game_session->buy_pending();
      // HUDModel.getButtonType: the Nexus has the Options button, every other map the Nexus button.
      state.header.button = cs.game_session && cs.game_session->game_id() != -1
                                ? realm::HudState::Header::Button::nexus
                                : realm::HudState::Header::Button::options;
      state.header.hover = !cs.trade.active() && cs.tooltip_pointer_valid && !cs.hud_mouse_down &&
                           !cs.options_screen.visible() && !cs.dialog.visible() && !cs.board.visible() &&
                           !cs.members.visible() && !(cs.skins && cs.skins->visible()) && !cs.interaction.visible() &&
                           realm::HudInput::button_at(realm::header_button, cs.hud_input.pointer());
      const auto panel_button = state.panel_button(cs.fonts);
      const bool hovered =
          panel_button.id >= 0 && realm::HudInput::button_at(panel_button.area, cs.hud_input.pointer());
      state.portal.hover = hovered && state.portal.id == panel_button.id;
      state.button.hover = hovered && state.button.id == panel_button.id;
      state.sellable.hover = hovered && state.sellable.id == panel_button.id;
      state.social.hover = hovered && state.social.id == panel_button.id;
      if (cs.hud->update(state))
        for (const auto& area : cs.hud->changes())
          cs.hud_texture.update_region(cs.hud->image(), area.x, area.y, area.width, area.height);
      cs.hud_view = state;
      if (cs.tooltip) {
        // EquipmentToolTip for trade slots replaces the hidden HUD grids.
        std::optional<realm::ItemTooltipTarget> target;
        if (cs.tooltip_pointer_valid && !cs.dialog.visible() && !cs.board.visible() && !cs.members.visible() &&
            !(cs.skins && cs.skins->visible()) && !cs.hud_mouse_down && !cs.interaction.visible()) {
          const auto pointer = cs.hud_input.pointer();
          if (cs.trade.active() && pointer.y >= realm::TradeView::panel_top) {
            int side = 0, slot = realm::TradeView::slot_at(pointer);
            if (slot < 0) {
              side = 1;
              slot = realm::TradeView::their_slot_at(pointer);
            }
            const auto& items = (side == 0 ? cs.trade.mine() : cs.trade.theirs()).items;
            if (slot >= 0 && items.at(static_cast<std::size_t>(slot)).item > 0)
              target = realm::item_tooltip_target(
                  cs.world, realm::InventoryItem{items.at(static_cast<std::size_t>(slot)).item, -1},
                  realm::TooltipOwner::other_player);
          } else if (!(cs.trade.request() && pointer.y >= realm::TradeView::request_top))
            target = realm::item_tooltip_target(cs.world, state, pointer);
        }
        if (target) target->use_special = cs.settings.get("useSpecial");
        cs.fonts.set_pixel_scale(std::min(height / 600.0, (width - 8.0) / realm::ItemTooltip::width));
        if (cs.tooltip->update(target)) cs.tooltip_texture.update(cs.tooltip->image());
        cs.fonts.set_pixel_scale(height / 600.0);
      }
      const double ui_scale = height / 600.0;
      overlay = {};
      // MapOverlay balloons sit beneath the HUD, chat and popups, in map pixels.
      cs.fonts.set_pixel_scale(cs.zoom);
      cs.balloons.update(cs.world, cs.fonts, time);
      cs.fonts.set_pixel_scale(height / 600.0);
      if (cs.settings.flag("textBubbles"))
        for (const auto& balloon : cs.balloons.balloons()) {
          const auto* speaker = cs.world.entity(balloon.object_id);
          if (!speaker) continue;
          auto& texture = cs.balloon_textures.at(balloon.slot);
          if (cs.balloon_uploaded.at(balloon.slot) != balloon.revision) {
            texture.update(balloon.image);
            cs.balloon_uploaded.at(balloon.slot) = balloon.revision;
          }
          const auto at = realm::SpeechBalloons::position(balloon, cs.camera.world_to_screen(speaker->motion.position),
                                                          speaker->appearance.size, cs.zoom);
          const double w = balloon.image.width / balloon.image.pixel_scale * cs.zoom,
                       h = balloon.image.height / balloon.image.pixel_scale * cs.zoom;
          overlay_storage[overlay.size()] =
              texture.quad(at, {w, 0}, {0, h}, 0, 0, balloon.image.width, balloon.image.height);
          overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
        }
      // CharacterStatusText: damage, conditions, notifications and level/exp/fame text float above the balloons.
      for (auto& pending : cs.world.take_status_events()) cs.status_texts.add(cs.world, std::move(pending));
      // Fixtures with a fixed effect clock age their text by --status-age after the first frame.
      const std::int32_t status_time = o.effect_time >= 0 ? o.effect_time + (cs.frame > 0 ? o.status_age : 0) : time;
      cs.fonts.set_pixel_scale(cs.zoom);
      cs.status_texts.update(cs.world, cs.status_art, status_time);
      cs.fonts.set_pixel_scale(height / 600.0);
      for (const auto& text : cs.status_texts.texts()) {
        if (!realm::StatusTexts::visible(text, status_time) || !text.label.image || overlay.size() >= 100) continue;
        const auto* speaker = cs.world.entity(text.object_id);
        if (!speaker) continue;
        const auto at = realm::StatusTexts::position(text, cs.camera.world_to_screen(speaker->motion.position),
                                                     speaker->appearance.size, cs.zoom, status_time);
        const double w = text.label.image->width / text.label.image->pixel_scale * cs.zoom,
                     h = text.label.image->height / text.label.image->pixel_scale * cs.zoom;
        const auto placed = cs.scene.status_quad(text.label.image, cs.atlas, at, {w, 0}, {0, h});
        std::shared_ptr<realm::SurfaceTexture> surface;
        if (!placed) surface = cs.scene.status_surface(text.label.image);
        auto quad = placed ? *placed : surface->quad(at, {w, 0}, {0, h});
        quad.owner = surface;
        overlay_storage[overlay.size()] = quad;
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
      if (cs.chat_view) {
        if (cs.chat_view->update(cs.chat, time) && !cs.chat_view->empty())
          cs.chat_texture.update(cs.chat_view->image());
        if (!cs.chat_view->empty()) {
          const auto& image = cs.chat_view->image();
          auto chat_quad =
              cs.chat_texture.quad({0, cs.chat_view->top() * ui_scale}, {realm::ChatView::width * ui_scale, 0},
                                   {0, cs.chat_view->surface_height() * ui_scale}, 0, 0, image.width, image.height);
          overlay_storage[overlay.size()] = chat_quad;
          overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
        }
      }
      auto q = cs.hud_texture.quad({width - 200 * ui_scale, 0}, {200 * ui_scale, 0}, {0, static_cast<double>(height)});
      q.kind = 5;
      overlay_storage[overlay.size()] = q;
      overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      if (cs.hud_art) {
        const int credits = player->profile.stats.value_or(realm::flash::StatId::CREDITS_STAT, 0),
                  fame = player->profile.stats.value_or(realm::flash::StatId::FAME_STAT, 0);
        if (credits != cs.displayed_credits || fame != cs.displayed_fame || cs.credit_density != ui_scale) {
          cs.credit_image = realm::render_credit_display(cs.fonts, *cs.hud_art, credits, fame);
          cs.credit_texture.update(cs.credit_image.image);
          cs.displayed_credits = credits;
          cs.displayed_fame = fame;
          cs.credit_density = ui_scale;
        }
        overlay_storage[overlay.size()] =
            cs.credit_texture.quad({width - 206 * ui_scale + cs.credit_image.origin_offset.x * ui_scale,
                                    cs.credit_image.origin_offset.y * ui_scale},
                                   {static_cast<double>(cs.credit_image.image.width), 0},
                                   {0, static_cast<double>(cs.credit_image.image.height)});
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
      if (cs.hud_art && cs.game_session && cs.game_session->map_info().show_displays) {
        const int stars = player->profile.stats.value_or(realm::flash::StatId::NUM_STARS_STAT, -1),
                  rank = player->profile.stats.value_or(realm::flash::StatId::GUILD_RANK_STAT, 0);
        if (stars != cs.displayed_stars || rank != cs.displayed_guild_rank ||
            player->profile.guild != cs.displayed_guild || cs.safe_area_density != ui_scale) {
          cs.safe_area_image =
              realm::render_safe_area_display(cs.fonts, *cs.hud_art, stars, player->profile.guild, rank);
          cs.safe_area_texture.update(cs.safe_area_image.image);
          cs.displayed_stars = stars;
          cs.displayed_guild_rank = rank;
          cs.displayed_guild = player->profile.guild;
          cs.safe_area_density = ui_scale;
        }
        overlay_storage[overlay.size()] = cs.safe_area_texture.quad(
            {cs.safe_area_image.origin_offset.x * ui_scale, cs.safe_area_image.origin_offset.y * ui_scale},
            {static_cast<double>(cs.safe_area_image.image.width), 0},
            {0, static_cast<double>(cs.safe_area_image.image.height)});
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
      if (cs.minimap) {
        overlay_storage[overlay.size()] = cs.minimap_gpu.build(*cs.minimap, cs.world, *player, cs.angle,
                                                               {width - 196 * ui_scale, 4 * ui_scale}, ui_scale);
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
      if (cs.trade_view) {
        cs.trade.update(time);
        if (cs.trade_view->update(cs.trade, cs.hud_input.pointer(), time)) {
          if (cs.trade.active())
            cs.trade_texture.update(cs.trade_view->panel());
          else if (cs.trade.request())
            cs.trade_request_texture.update(cs.trade_view->request());
        }
        if (cs.trade.active() || cs.trade.request()) {
          const bool panel = cs.trade.active();
          const auto& image = panel ? cs.trade_view->panel() : cs.trade_view->request();
          const double top = panel ? realm::TradeView::panel_top : realm::TradeView::request_top,
                       h = panel ? realm::TradeView::panel_height : realm::TradeView::request_height;
          auto trade_quad = (panel ? cs.trade_texture : cs.trade_request_texture)
                                .quad({width - 200 * ui_scale, top * ui_scale}, {200 * ui_scale, 0}, {0, h * ui_scale},
                                      0, 0, image.width, image.height);
          overlay_storage[overlay.size()] = trade_quad;
          overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
        }
      }
      if (cs.hud_art && cs.hud_input.dragging() >= 0) {
        const auto slot = static_cast<std::size_t>(cs.hud_input.dragging());
        const int type = state.inventory.items.at(slot);
        if (type > 0) {
          const auto key = (slot < 4 && state.inventory.dim.at(slot) ? "dim" : "item") + std::to_string(type);
          if (cs.drag_key != key) {
            const auto& icon = cs.hud_art->image(key);
            if (icon.width > 128 || icon.height > 128) throw std::runtime_error("Drag icon exceeds reserved image");
            std::fill(cs.drag_bitmap.pixels.begin(), cs.drag_bitmap.pixels.end(), 0);
            for (int y = 0; y < icon.height; ++y)
              for (int x = 0; x < icon.width; ++x) {
                const auto src =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(icon.width) + static_cast<std::size_t>(x)) *
                    4;
                const auto dst = (static_cast<std::size_t>(y + 64 - icon.height / 2) * 128 +
                                  static_cast<std::size_t>(x + 64 - icon.width / 2)) *
                                 4;
                const unsigned alpha = icon.pixels[src + 3];
                cs.drag_bitmap.pixels[dst + 3] = static_cast<unsigned char>(alpha);
                for (unsigned c = 0; c < 3; ++c)
                  cs.drag_bitmap.pixels[dst + c] =
                      static_cast<unsigned char>(alpha == 255 ? icon.pixels[src + c]
                                                 : alpha ? static_cast<unsigned>(icon.pixels[src + c]) * 256 / alpha
                                                         : 0);
              }
            cs.drag_surface.update(cs.drag_bitmap);
            cs.drag_key = key;
          }
          const auto pointer = cs.hud_input.pointer();
          auto drag = cs.drag_surface.quad({width + (pointer.x - 264) * ui_scale, (pointer.y - 64) * ui_scale},
                                           {128 * ui_scale, 0}, {0, 128 * ui_scale});
          drag.kind = 5;
          overlay_storage[overlay.size()] = drag;
          overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
        }
      }
      if (cs.tooltip && cs.tooltip->visible()) {
        const auto pointer = cs.hud_input.pointer();
        const realm::Vec2 screen_pointer{width + (pointer.x - 200) * ui_scale, pointer.y * ui_scale};
        const double scale = std::min(ui_scale, (width - 8.0) / realm::ItemTooltip::width);
        const auto at = realm::ItemTooltip::position(screen_pointer, width, height, cs.tooltip->height(), scale);
        auto tip = cs.tooltip_texture.quad(
            at, {realm::ItemTooltip::width * scale, 0}, {0, cs.tooltip->height() * scale}, 0, 0,
            cs.tooltip->image().width,
            static_cast<int>(std::ceil(cs.tooltip->height() * cs.tooltip->image().pixel_scale)));
        overlay_storage[overlay.size()] = tip;
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
      if (cs.player_tooltip && cs.player_tooltip->visible()) {
        const auto pointer = cs.hud_input.pointer();
        const realm::Vec2 screen_pointer{width + (pointer.x - 200) * ui_scale, pointer.y * ui_scale};
        const double scale = ui_scale;
        const auto at = realm::PlayerTooltip::position(screen_pointer, width, height, cs.player_tooltip->draw_width(),
                                                       cs.player_tooltip->height(), scale);
        const double density = cs.player_tooltip->image().pixel_scale;
        auto tip = cs.player_tooltip_texture.quad(
            at, {cs.player_tooltip->draw_width() * scale, 0}, {0, cs.player_tooltip->height() * scale}, 0, 0,
            static_cast<int>(std::ceil(cs.player_tooltip->draw_width() * density)),
            static_cast<int>(std::ceil(cs.player_tooltip->height() * density)));
        overlay_storage[overlay.size()] = tip;
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
    }
}

void build_group_overlays(ClientState& cs, const Options& o, Frame& f) {
  auto& time = f.time;
  auto& menus = f.menus;
  auto& width = f.width;
  auto& height = f.height;
  auto& overlay_storage = f.overlay_storage;
  auto& overlay = f.overlay;
  if (cs.player_group && cs.hud && cs.minimap && o.world && !menus) {
    const auto pointer = cs.hud_input.pointer();
    const double scale = height / 600.0;
    const realm::Vec2 screen_pointer{width + (pointer.x - 200) * scale, pointer.y * scale};
    const bool enabled = !cs.dialog.visible() && !cs.board.visible() && !cs.members.visible() &&
                         !(cs.skins && cs.skins->visible()) && !cs.interaction.visible() && cs.game_session &&
                         cs.game_session->state() == realm::SessionState::playing;
    if (!enabled) {
      cs.player_group->close();
      cs.party_arrows.reset();
    } else {
      cs.party_arrows.update(cs.world, cs.game_session->party(), cs.camera, screen_pointer, cs.tooltip_pointer_valid,
                             width, height);
      for (std::size_t i = 0; i < cs.party_arrows.arrows().size(); ++i) {
        const auto& arrow = cs.party_arrows.arrows()[i];
        if (arrow.players.empty()) continue;
        const double density = std::max(1.0, cs.camera.scale());
        if (cs.arrow_densities[i] != density || cs.arrow_rotations[i] != arrow.rotation ||
            cs.arrow_hovers[i] != arrow.hover) {
          cs.arrow_textures[i].update(realm::PartyArrows::image(arrow.rotation, arrow.hover, density));
          cs.arrow_densities[i] = density;
          cs.arrow_rotations[i] = arrow.rotation;
          cs.arrow_hovers[i] = arrow.hover;
        }
        const double a_scale = cs.camera.scale();
        const auto quad = cs.arrow_textures[i].quad({arrow.position.x - realm::PartyArrows::origin * a_scale,
                                                     arrow.position.y - realm::PartyArrows::origin * a_scale},
                                                    {realm::PartyArrows::canvas_size * a_scale, 0},
                                                    {0, realm::PartyArrows::canvas_size * a_scale});
        overlay_storage[overlay.size()] = quad;
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
    }
    if (enabled && cs.player_group->menu()) {
      if (cs.player_group->refresh_appearance(cs.world)) cs.group_texture.update(cs.player_group->image());
      if (cs.player_group->update(screen_pointer, cs.tooltip_pointer_valid, cs.game_session->teleport_cooldown(time),
                                  scale))
        cs.group_texture.update(cs.player_group->image());
    } else if (enabled && cs.party_arrows.hovered() && !(cs.player_menu && cs.player_menu->visible())) {
      const auto& arrow = *cs.party_arrows.hovered();
      cs.fonts.set_pixel_scale(scale * cs.camera.scale());
      if (cs.player_group->arrow_tooltip(realm::player_group_targets(cs.world, arrow.players), arrow.position,
                                         arrow.rotation, cs.camera.scale(), scale * cs.camera.scale()))
        cs.group_texture.update(cs.player_group->image());
      cs.fonts.set_pixel_scale(scale);
    } else if (enabled)
      if (const auto* focus = cs.world.entity(cs.world.player_id())) {
        const auto ids = (!cs.tooltip_pointer_valid || (cs.player_menu && cs.player_menu->visible()))
                             ? std::vector<std::int32_t>{}
                             : cs.minimap->hover_players(cs.world, *focus, {pointer.x - 4, pointer.y - 4});
        if (cs.player_group->tooltip(realm::player_group_targets(cs.world, ids), screen_pointer, width, height, scale))
          cs.group_texture.update(cs.player_group->image());
      }
    if (cs.player_group->visible()) {
      const auto& image = cs.player_group->image();
      auto quad = cs.group_texture.quad(
          cs.player_group->canvas_position(), {cs.player_group->canvas_width() * cs.player_group->scale(), 0},
          {0, cs.player_group->canvas_height() * cs.player_group->scale()}, 0, 0, image.width, image.height);
      overlay_storage[overlay.size()] = quad;
      overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    }
  }
  if (cs.player_menu && cs.player_menu->visible() && cs.hud && o.world && !menus) {
    const auto pointer = cs.hud_input.pointer();
    const double ui_scale = height / 600.0;
    const realm::Vec2 screen_pointer{width + (pointer.x - 200) * ui_scale, pointer.y * ui_scale};
    const bool valid = cs.tooltip_pointer_valid && !cs.dialog.visible() && !cs.board.visible() &&
                       !cs.members.visible() && !(cs.skins && cs.skins->visible()) && !cs.interaction.visible();
    if (cs.player_menu->update(cs.world, screen_pointer, valid,
                               cs.game_session ? cs.game_session->teleport_cooldown(time) : 0, ui_scale))
      cs.player_menu_texture.update(cs.player_menu->image());
    if (cs.player_menu->visible()) {
      const auto& image = cs.player_menu->image();
      const double scale = cs.player_menu->scale();
      auto menu_quad =
          cs.player_menu_texture.quad(cs.player_menu->canvas_position(), {realm::PlayerMenu::canvas_width * scale, 0},
                                      {0, realm::PlayerMenu::canvas_height * scale}, 0, 0, image.width, image.height);
      overlay_storage[overlay.size()] = menu_quad;
      overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    }
  }
}

void build_modal_overlays(ClientState& cs, const Options& o, Frame& f) {
  auto& time = f.time;
  auto& menus = f.menus;
  auto& width = f.width;
  auto& height = f.height;
  auto& overlay_storage = f.overlay_storage;
  auto& overlay = f.overlay;
  if (cs.interaction.visible() && !menus) {
    cs.fonts.set_pixel_scale(realm::InteractionDialog::scale(width, height));
    if (cs.interaction.render(cs.fonts)) cs.interaction_texture.update(cs.interaction.image());
    cs.fonts.set_pixel_scale(height / 600.0);
    const auto at = realm::InteractionDialog::position(width, height);
    const double scale = realm::InteractionDialog::scale(width, height);
    auto dim = realm::Quad{};
    dim.origin = {0, 0};
    dim.axis_x = {static_cast<float>(width), 0};
    dim.axis_y = {0, static_cast<float>(height)};
    dim.kind = 2;
    dim.multiply = {0, 0, 0, .55F};
    overlay_storage[overlay.size()] = dim;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    auto frame_quad = cs.interaction_texture.quad(at, {realm::InteractionDialog::width * scale, 0},
                                                  {0, realm::InteractionDialog::height * scale});
    frame_quad.kind = 5;
    overlay_storage[overlay.size()] = frame_quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
  if (cs.board.visible() && !menus) {
    if (cs.board.update(cs.fonts, width, height)) cs.board_texture.update(cs.board.image());
    if (cs.board.backdrop()) {
      auto dim = realm::Quad{};
      dim.origin = {0, 0};
      dim.axis_x = {static_cast<float>(width), 0};
      dim.axis_y = {0, static_cast<float>(height)};
      dim.kind = 2;
      dim.multiply = {0, 0, 0, .8F};
      overlay_storage[overlay.size()] = dim;
      overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    }
    const auto& image = cs.board.image();
    auto frame_quad =
        cs.board_texture.quad(cs.board.canvas_position(), {image.width * cs.board.scale() / image.pixel_scale, 0},
                              {0, image.height * cs.board.scale() / image.pixel_scale});
    overlay_storage[overlay.size()] = frame_quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
  if (cs.members.visible() && !menus) {
    if (cs.members.update(cs.fonts, width, height)) cs.members_texture.update(cs.members.image());
    auto quad = cs.members_texture.quad({0, 0}, {static_cast<double>(width), 0}, {0, static_cast<double>(height)});
    overlay_storage[overlay.size()] = quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
  if (cs.skins && cs.skins->visible() && !menus) {
    if (cs.skins->update(width, height, time)) {
      const auto r = cs.skins->change();
      cs.skin_texture.update_region(cs.skins->image(), r.x, r.y, r.width, r.height);
    }
    auto quad = cs.skin_texture.quad({0, 0}, {static_cast<double>(width), 0}, {0, static_cast<double>(height)});
    overlay_storage[overlay.size()] = quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
  if (cs.options_screen.visible() && !menus) {
    if (cs.options_screen.update(cs.fonts, width, height, time)) cs.options_texture.update(cs.options_screen.image());
    auto quad = cs.options_texture.quad({0, 0}, {static_cast<double>(width), 0}, {0, static_cast<double>(height)});
    overlay_storage[overlay.size()] = quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
  if (cs.perf_stats && o.world && !menus) {
    // Toggle Performance Stats: recent FPS, rolling frame-time percentiles and resources.
    if (cs.perf_dirty && !cs.perf_text.empty()) {
      const double ui = height / 600.0;
      constexpr int value_size = 12;
      constexpr double label_scale = 10.0 / value_size;
      constexpr unsigned outline_strength = 3;
      constexpr double column_gap = 6, row_gap = 1;
      const double label_y = cs.fonts.baseline(value_size, true) * (1 - label_scale);
      const auto rasterize_label = [&](const std::string& label_text) {
        // Render smaller labels directly from the native font at drawable density.
        cs.fonts.set_pixel_scale(ui * label_scale);
        auto label = cs.fonts.rasterize_glow(label_text, value_size, true, 0xd7dce2, outline_strength);
        cs.fonts.set_pixel_scale(ui);
        label.width *= label_scale;
        label.height *= label_scale;
        label.offset.x *= label_scale;
        label.offset.y *= label_scale;
        label.image.pixel_scale = ui;
        label.redraw.reset();
        return label;
      };
      struct PerfRow {
        realm::TextBitmap label, value;
        double height;
        bool regular;
      };
      std::vector<PerfRow> rows;
      double label_w = 0, value_w = 0, regular_w = 0, box_h = 0;
      std::istringstream in(cs.perf_text);
      for (std::string line; std::getline(in, line);) {
        const auto split = line.find(' ');
        if (split == std::string::npos) continue;
        const bool regular = line.ends_with(" FPS") || line.ends_with(" MB");
        auto label = regular ? realm::TextBitmap{} : rasterize_label(line.substr(0, split));
        auto value = cs.fonts.rasterize_glow(regular ? line : line.substr(split + 1), value_size, true, 0xffffff,
                                             outline_strength);
        const double row_h = regular ? value.height : std::max(label.height + label_y, value.height);
        if (regular)
          regular_w = std::max(regular_w, value.width);
        else {
          label_w = std::max(label_w, label.width);
          value_w = std::max(value_w, value.width);
        }
        box_h += row_h + row_gap;
        rows.push_back({std::move(label), std::move(value), row_h, regular});
      }
      // Light black outlines and smaller frame-time labels aligned to the value baseline.
      auto image = realm::ui_image(std::max(regular_w, label_w + column_gap + value_w) + 8, box_h + 8, ui);
      double row_y = 4;
      for (const auto& row : rows) {
        if (!row.regular)
          realm::ui_blit_text(image, row.label, 4 + label_w - row.label.width + row.label.offset.x,
                              row_y + label_y + row.label.offset.y);
        const double value_x = row.regular ? 4 : 4 + label_w + column_gap;
        realm::ui_blit_text(image, row.value, value_x + row.value.offset.x, row_y + row.value.offset.y);
        row_y += row.height + row_gap;
      }
      cs.perf_size = {static_cast<double>(image.width), static_cast<double>(image.height)};
      cs.perf_texture.update(image);
      cs.perf_dirty = false;
    }
    if (!cs.perf_text.empty()) {
      auto quad =
          cs.perf_texture.quad({4.0 * height / 600, 4.0 * height / 600}, {cs.perf_size.x, 0}, {0, cs.perf_size.y});
      overlay_storage[overlay.size()] = quad;
      overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    }
  }
  if (cs.hud && o.world && !menus && cs.hud_view.header.hover && !cs.options_screen.visible()) {
    // IconButton: TextToolTip(0x363636, 0x9B9B9B, title, "Hotkey: KEY", 200) at the pointer.
    const bool nexus = cs.hud_view.header.button == realm::HudState::Header::Button::nexus;
    const std::string title = nexus ? "Nexus" : "Options",
                      text = "Hotkey: " +
                             std::string(realm::key_name(cs.settings.get(nexus ? "escapeToNexus" : "options")));
    const auto key = title + "|" + text + "|" + std::to_string(height);
    if (key != cs.header_tip_key) {
      cs.header_tip = realm::render_text_tooltip(cs.fonts, title, text, 3552822, 10197915, 200);
      cs.header_tip_texture.update(cs.header_tip.image);
      cs.header_tip_key = key;
    }
    const double ui = height / 600.0;
    const auto pointer = cs.hud_input.pointer();
    const auto at =
        realm::text_tooltip_position({width + (pointer.x - 200) * ui, pointer.y * ui}, cs.header_tip.frame_width,
                                     cs.header_tip.frame_height, width, height, ui);
    const auto& image = cs.header_tip.image;
    auto quad = cs.header_tip_texture.quad({at.x - cs.header_tip.margin * ui, at.y - cs.header_tip.margin * ui},
                                           {static_cast<double>(image.width), 0},
                                           {0, static_cast<double>(image.height)}, 0, 0, image.width, image.height);
    overlay_storage[overlay.size()] = quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
  if (cs.dialog.visible() && !menus) {
    if (cs.dialog.update(cs.fonts, width, height)) cs.dialog_texture.update(cs.dialog.image());
    auto dim = realm::Quad{};
    dim.origin = {0, 0};
    dim.axis_x = {static_cast<float>(width), 0};
    dim.axis_y = {0, static_cast<float>(height)};
    dim.kind = 2;
    dim.multiply = {21 / 255.F, 21 / 255.F, 21 / 255.F, .6F};
    overlay_storage[overlay.size()] = dim;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    const auto& image = cs.dialog.image();
    auto frame_quad =
        cs.dialog_texture.quad(cs.dialog.canvas_position(), {image.width * cs.dialog.scale() / image.pixel_scale, 0},
                               {0, image.height * cs.dialog.scale() / image.pixel_scale});
    overlay_storage[overlay.size()] = frame_quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
}

void present_frame(ClientState& cs, const Options& o, Frame& f) {
  auto& start = f.start;
  auto& frame_interval = f.frame_interval;
  auto& input_age = f.input_age;
  auto& input_observed = f.input_observed;
  auto& previous_drops = f.previous_drops;
  auto& time = f.time;
  auto& script_capture = f.script_capture;
  auto& width = f.width;
  auto& height = f.height;
  auto& clip_width = f.clip_width;
  auto& cpu_zone = f.cpu_zone;
  auto& ui_zone = f.ui_zone;
  auto& overlay = f.overlay;
  realm::ScreenEffects effects;
  if (o.world && (cs.game_session || !o.map.empty())) {
    const realm::Entity* local = cs.world.entity(cs.world.player_id());
    effects = realm::screen_effects(local, o.effect_time >= 0 ? o.effect_time : time, height / 600.0);
  }
  ui_zone->finish();
  std::vector<realm::Quad> native_ui;
  if (cs.production_ui) {
    REALM_ZONE("ui.documents.update");
    native_ui = cs.production_ui->compose(overlay, time, cs.ui_viewport);
    overlay = native_ui;
  }
  const bool capture_frame =
      (!o.capture.empty() && (!o.frames ? cs.frame == 0 : cs.frame + 1 == static_cast<std::uint64_t>(o.frames))) ||
      !script_capture.empty();
  const bool normal_frame = cs.frame >= static_cast<std::uint64_t>(o.telemetry_warmup) && !capture_frame;
  cs.renderer.timing(cs.telemetry.get(), cs.frame, normal_frame);
  cs.renderer.draw(cs.atlas, cs.quads, width, height, clip_width, overlay, effects);
  if (cs.production_ui) {
    REALM_ZONE("ui.documents.render");
    cs.production_ui->render(width, height);
    using C = realm::telemetry::Counter;
    const auto stats = cs.production_ui->host().stats();
    realm::telemetry::set(C::ui_text_rasterizations,
                          stats.text_rasterizations - cs.previous_ui_stats.text_rasterizations);
    realm::telemetry::set(C::ui_painter_rasterizations,
                          stats.painter_rasterizations - cs.previous_ui_stats.painter_rasterizations);
    realm::telemetry::set(C::ui_texture_uploads, stats.texture_uploads - cs.previous_ui_stats.texture_uploads);
    realm::telemetry::set(C::ui_texture_upload_bytes,
                          stats.texture_upload_bytes - cs.previous_ui_stats.texture_upload_bytes);
    realm::telemetry::set(C::ui_text_meshes, stats.text_meshes - cs.previous_ui_stats.text_meshes);
    realm::telemetry::set(C::ui_native_image_meshes,
                          stats.native_image_meshes - cs.previous_ui_stats.native_image_meshes);
    realm::telemetry::set(C::ui_native_image_bytes, stats.native_image_bytes);
    realm::telemetry::set(C::ui_documents, stats.documents);
    realm::telemetry::set(C::ui_draw_calls, stats.draws);
    realm::telemetry::add(C::draw_calls, stats.draws);
    cs.previous_ui_stats = stats;
  }
  if (cs.death_background && cs.startup) {
    const auto alpha = static_cast<float>(cs.startup->death_fade(time));
    if (alpha > 0) {
      auto snapshot =
          cs.death_background->quad({0, 0}, {static_cast<double>(width), 0}, {0, static_cast<double>(height)});
      snapshot.multiply = {1, 1, 1, alpha};
      cs.renderer.draw_overlay(cs.atlas, std::span<const realm::Quad>(&snapshot, 1), width, height);
    } else
      cs.death_background.reset();
  }
  const auto submitted = Clock::now();
  cpu_zone->finish();

  // Capture before swap; finite runs always capture their last rendered frame.
  if (!o.capture.empty() && (!o.frames ? cs.frame == 0 : cs.frame + 1 == static_cast<std::uint64_t>(o.frames)))
    realm::save_png(o.capture, cs.renderer.capture(width, height));
  if (!script_capture.empty()) {
    realm::save_png(script_capture, cs.renderer.capture(width, height));
    if (o.benchmark)
      std::cout << "Particle capture " << script_capture.filename()
                << " particles=" << cs.world.particles().particles().size()
                << " emitters=" << cs.world.particles().emitter_count()
                << " flashes=" << cs.world.particles().flashes().size() << " dropped=" << cs.world.particles().dropped()
                << " quads=" << cs.quads.size() << '\n';
  }
  const auto present_start = Clock::now();
  {
    REALM_ZONE("present.swap");
    SDL_GL_SwapWindow(cs.session.window);
  }
  const auto presented = Clock::now();
  const int warmup = o.frames > 60 ? 30 : 0;
  if (o.frames && cs.frame >= static_cast<std::uint64_t>(warmup) && !capture_frame) {
    cs.cpu_samples.push_back(milliseconds(submitted - start));
    cs.present_samples.push_back(milliseconds(presented - present_start));
  }
  if (o.frames && cs.frame + 1 == static_cast<std::uint64_t>(o.frames))
    std::cout << "Last frame quads=" << cs.quads.size() + overlay.size() << " draws=" << cs.renderer.draw_calls()
              << " GPU_last_ms=" << cs.renderer.gpu_ms() << '\n';
  realm::check_graphics_error();
  if (cs.perf_stats) {
    // Measure completed swap intervals, including pacing and stalls;
    // simulation dt can be clamped or use a fixed fixture clock.
    if (cs.perf_previous_present) {
      const double interval = milliseconds(presented - *cs.perf_previous_present);
      ++cs.perf_frames;
      cs.perf_clock += interval;
      cs.perf_history.push_back({presented, interval});
      while (milliseconds(presented - cs.perf_history.front().presented) >= cs.perf_history_ms)
        cs.perf_history.pop_front();
    }
    cs.perf_previous_present = presented;
    if (cs.perf_clock >= cs.perf_refresh_ms) {
      // Reuse the scratch storage and calculate tails only when the text refreshes.
      cs.perf_intervals.clear();
      for (const auto& sample : cs.perf_history) cs.perf_intervals.push_back(sample.interval);
      // Nearest-rank percentiles; progressively partition just the remaining tail.
      std::size_t percentile_begin{};
      const auto percentile = [&](double fraction) {
        const auto index =
            static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(cs.perf_intervals.size()))) - 1;
        std::nth_element(cs.perf_intervals.begin() + percentile_begin, cs.perf_intervals.begin() + index,
                         cs.perf_intervals.end());
        percentile_begin = index;
        return cs.perf_intervals[index];
      };
      const double p90 = percentile(.90), p99 = percentile(.99);
      std::ostringstream text;
      text << std::fixed << std::setprecision(1) << cs.perf_frames * 1000 / cs.perf_clock << " FPS";
      if (const double mb = realm::platform::resident_megabytes(); mb > 0) text << '\n' << mb << " MB";
      text << std::setprecision(3) << "\navg " << cs.perf_clock / cs.perf_frames << " ms\n"
           << "p90 " << p90 << " ms\np99 " << p99 << " ms";
      cs.perf_text = text.str();
      cs.perf_dirty = true;
      cs.perf_frames = 0;
      cs.perf_clock = 0;
    }
  }
  // Max FPS: VSync alone does not cap a fast display or an unsynchronized swap.
  if (!o.benchmark && !o.hidden && cs.settings.get("maxFPS") > 0) {
    const double target = 1000.0 / cs.settings.get("maxFPS");
    while (milliseconds(Clock::now() - start) < target) {
      if (target - milliseconds(Clock::now() - start) > 2) SDL_Delay(1);
    }
  }
  if (cs.telemetry) {
    using C = realm::telemetry::Counter;
    realm::telemetry::set(C::resident_entities, cs.world.entities().size());
    realm::telemetry::set(C::resident_projectiles, cs.game_session ? cs.game_session->projectile_count()
                                                   : cs.stress     ? cs.stress->projectile_count()
                                                                   : 0);
    realm::telemetry::set(C::outgoing_queue_bytes, cs.game_session ? cs.game_session->outgoing_queue_bytes() : 0);
    realm::telemetry::set(C::framing_queue_bytes, cs.game_session ? cs.game_session->framing_queue_bytes() : 0);
    realm::telemetry::set(C::particles, cs.world.particles().particles().size());
    realm::telemetry::set(C::appearance_cpu_bytes, cs.appearances.bytes());
    realm::telemetry::set(C::appearance_gpu_bytes, cs.scene.appearance_bytes());
    realm::telemetry::set(C::glyph_bytes, cs.fonts.cache_bytes());
    const auto drops = cs.world.particles().dropped();
    realm::telemetry::set(C::optional_drops, drops >= previous_drops ? drops - previous_drops : drops);
    // Capture/readback is excluded from CPU/present and marked on the
    // frame. The following interval also includes capture work.
    cs.telemetry->end({cs.frame, cs.frame < static_cast<std::uint64_t>(o.telemetry_warmup), capture_frame,
                       cs.frame > 0 && !capture_frame && !cs.previous_capture, frame_interval,
                       milliseconds(submitted - start), milliseconds(presented - present_start),
                       milliseconds(Clock::now() - presented),
                       input_observed ? std::max(0.0, input_age + milliseconds(submitted - start)) : -1, width, height,
                       SDL_GL_GetSwapInterval(), o.benchmark || o.hidden ? 0 : cs.settings.get("maxFPS")});
  }
  if (cs.stress) {
    cs.stress->sample_scene(cs.camera, width, height, cs.quads, cs.scene.submitted_counts());
    const double rss = (cs.frame % 50 == 0 || cs.frame + 1 == static_cast<std::uint64_t>(o.frames))
                           ? realm::platform::resident_megabytes()
                           : 0;
    cs.stress->sample_resources(rss, cs.appearances.entries(), cs.appearances.bytes(), cs.scene.appearance_entries(),
                                cs.scene.appearance_bytes());
  }
  if ((cs.stress || o.fixture_step) && o.stress_delay) SDL_Delay(static_cast<Uint32>(o.stress_delay));
  cs.previous_capture = capture_frame;
}

void shutdown_report(ClientState& cs, const Options& o) {
  if (cs.telemetry) {
    cs.renderer.poll_gpu();
    cs.telemetry->metadata("gpu_pending_at_exit", std::to_string(cs.renderer.pending_gpu()));
    cs.telemetry->metadata("gpu_skipped_busy_slots", std::to_string(cs.renderer.skipped_gpu()));
    cs.telemetry->finish();
  }
  if (cs.production_ui) {
    const auto s = cs.production_ui->host().stats();
    std::cout << "UI documents=" << s.documents << " loads=" << s.document_loads << " load_us=" << s.document_load_us
              << " native_image_bytes=" << s.native_image_bytes << " text_cache_bytes=" << s.text_cache_bytes
              << " gpu_last_ms=" << s.gpu_ms << " warnings=" << s.log_warnings << " errors=" << s.log_errors << '\n';
  }
  if (cs.stress) cs.stress->write_summary(o.stress_summary);
  // Parameters.cameraAngle persists across sessions.
  if (cs.persist_settings && cs.settings.camera_angle() != cs.angle) cs.save_settings();
  std::cout << "Appearance CPU=" << cs.appearances.entries() << "/" << cs.appearances.bytes()
            << " GPU=" << cs.scene.appearance_entries() << "/" << cs.scene.appearance_bytes()
            << " evictions=" << cs.appearances.evictions() << '\n';
  std::cout << "Dynamic atlas pages=" << cs.atlas.dynamic_pages() << "/" << cs.dynamic_atlas_max_pages
            << " resizes=" << cs.atlas.resizes() << " resident=" << cs.scene.residency().entries() << "/"
            << cs.scene.residency().used_bytes() << " evictions=" << cs.scene.residency().evictions()
            << " fallbacks=" << cs.scene.atlas_fallbacks() << '\n';
  std::cout << std::fixed << std::setprecision(4);
  report("CPU event/build/submit", std::move(cs.cpu_samples));
  report("Present", std::move(cs.present_samples));
}

}  // namespace realm::application
