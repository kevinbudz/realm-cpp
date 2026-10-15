#include "ApplicationOptions.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace realm::application {
int integer(std::string_view value) {
  int result{};
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
  if (error != std::errc{} || end != value.data() + value.size()) {
    throw std::invalid_argument("Invalid integer: " + std::string(value));
  }
  return result;
}

Options parse(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto value = [&]() -> std::string {
      if (++i == argc) {
        throw std::invalid_argument("Missing value for " + arg);
      }
      return argv[i];
    };
    if (arg == "--flash-root") {
      options.root = value();
    } else if (arg == "--server") {
      options.session.host = value();
    } else if (arg == "--app-port") {
      const auto port = integer(value());
      if (port < 1 || port > 65535) {
        throw std::invalid_argument("Invalid app port");
      }
      options.app_port = static_cast<std::uint16_t>(port);
    } else if (arg == "--audio") {
      options.audio = value();
      if (options.audio != "auto" && options.audio != "on" && options.audio != "off" &&
          options.audio != "null") {
        throw std::invalid_argument("--audio expects auto, on, off or null");
      }
    } else if (arg == "--account-file") {
      options.account_file = value();
    } else if (arg == "--settings-file") {
      options.settings_file = value();
    } else if (arg == "--script") {
      options.script = value();
    } else if (arg == "--connect") {
      options.session.host = value();
      options.connect = true;
      options.client = false;
    } else if (arg == "--port") {
      const auto port = integer(value());
      if (port < 1 || port > 65535) {
        throw std::invalid_argument("Invalid port");
      }
      options.session.port = static_cast<std::uint16_t>(port);
    } else if (arg == "--username") {
      options.session.username = value();
    } else if (arg == "--password-stdin") {
      options.password_stdin = true;
    } else if (arg == "--character") {
      options.session.character_id = integer(value());
    } else if (arg == "--create") {
      const auto type = integer(value());
      if (type < 0 || type > 65535) {
        throw std::invalid_argument("Invalid class type");
      }
      options.session.create_character = true;
      options.session.class_type = static_cast<std::uint16_t>(type);
    } else if (arg == "--game") {
      options.session.game_id = integer(value());
    } else if (arg == "--width") {
      options.width = integer(value());
    } else if (arg == "--height") {
      options.height = integer(value());
    } else if (arg == "--frames") {
      options.frames = integer(value());
    } else if (arg == "--map") {
      options.map = value();
      options.client = false;
    } else if (arg == "--stress") {
      options.stress = value();
      options.client = false;
    } else if (arg == "--stress-summary") {
      options.stress_summary = value();
    } else if (arg == "--fixture-step-ms") {
      options.fixture_step = integer(value());
      if (options.fixture_step != 20) {
        throw std::invalid_argument("Fixture clock uses 20ms steps");
      }
    } else if (arg == "--stress-delay") {
      options.stress_delay = integer(value());
    } else if (arg == "--telemetry") {
      options.telemetry = value();
    } else if (arg == "--trace") {
      options.trace = value();
    } else if (arg == "--telemetry-warmup") {
      options.telemetry_warmup = integer(value());
      if (options.telemetry_warmup < 0) {
        throw std::invalid_argument("Invalid telemetry warmup");
      }
    } else if (arg == "--capture") {
      options.capture = value();
    } else if (arg == "--condition") {
      options.condition = static_cast<std::uint32_t>(std::stoul(value()));
    } else if (arg == "--breath") {
      options.breath = integer(value());
    } else if (arg == "--hp") {
      options.hp = integer(value());
    } else if (arg == "--scene-time") {
      options.scene_time = integer(value());
    } else if (arg == "--ghost") {
      options.ghost = true;
    } else if (arg == "--ally") {
      options.ally = true;
    } else if (arg == "--ally-name") {
      options.ally_name = value();
    } else if (arg == "--notify") {
      options.notify = value();
    } else if (arg == "--damage") {
      options.damage = integer(value());
    } else if (arg == "--damage-effects") {
      options.damage_effects = value();
    } else if (arg == "--status-age") {
      options.status_age = integer(value());
    } else if (arg == "--effect-time") {
      options.effect_time = integer(value());
    } else if (arg == "--hidden") {
      options.hidden = true;
    } else if (arg == "--benchmark") {
      options.benchmark = true;
    } else if (arg == "--help") {
      std::cout
          << "realm_client [--flash-root PATH] [--server HOST] [--port N] [--app-port N] "
             "[--account-file PATH]\n"
             "  [--settings-file PATH] [--width N] [--height N] [--hidden]\n"
             "  [--audio auto|on|off|null] (sound output; auto plays only in the client; "
             "REALM_AUDIO_DEVICE=null also "
             "selects null)\n"
             "Startup sequence (default): loading, title, character selection, then gameplay.\n"
             "  --server HOST and --port N choose the game server; --account-file PATH stores the "
             "account.\n"
             "  Escape in game returns to character selection.\n"
             "Move: WASD; Q/E rotate the view (with Allow Camera Rotation).\n"
             "Inventory: drag to swap/drop; double-click equip/use; Shift-click use; Ctrl-click "
             "backpack transfer; 1-8 "
             "use.\n"
             "Potions: F/V or Shift/double-click the health/magic stacks.\n"
             "Portals: click Enter or press 0 beside a portal.\n"
             "Options: O opens the Options screen (key bindings and settings). --settings-file "
             "PATH stores them\n"
             "  (default beside the account file; the test-only modes keep the defaults unless a "
             "file is given).\n"
             "\n"
             "Test-only options (for fixtures and tests, not players):\n"
             "Modes: each replaces the startup sequence and they cannot be combined with one "
             "another.\n"
             "  --connect HOST     direct live join; identity flags (connect only): --username "
             "NAME --password-stdin\n"
             "                     --character N, or --create CLASS_TYPE to create a character "
             "instead, and --game N.\n"
             "  --map PATH.jm|PATH.wmap   offline world. Fixtures: --hp N --condition N --breath N "
             "--ghost --ally\n"
             "                     --ally-name NAME --notify TEXT[:RRGGBB] --damage N "
             "--damage-effects LIST\n"
             "                     --status-age N --effect-time N --scene-time N\n"
             "  --stress MANIFEST.json   rendered stress run; needs --frames N and "
             "--stress-summary RESULT.json;\n"
             "                     --stress-delay MS (0-100).\n"
             "Run control: --frames N (--hidden needs --frames or --script); --benchmark (600 "
             "frames when none given,\n"
             "  no vsync or Max FPS); --fixture-step-ms 20 (fixed clock, needs finite --frames); "
             "--capture PATH.png.\n"
             "Telemetry: --telemetry PATH.jsonl [--trace PATH.json] [--telemetry-warmup N] (frame "
             "count).\n"
             "Input script: --script FILE replays lines 'MS click X Y', 'MS key "
             "escape|enter|tab|backspace',\n"
             "  'MS hold KEY' / 'MS release KEY' (a held key for polled controls),\n"
             "  'MS move X Y', 'MS text STRING', 'MS capture PATH.png' and 'MS quit' (MS since "
             "start, X/Y window "
             "points).\n";
      std::exit(0);
    } else {
      throw std::invalid_argument("Unknown option: " + arg);
    }
  }

  if (options.width < 64 || options.width > 8192 || options.height < 64 || options.height > 8192 ||
      options.frames < 0) {
    throw std::invalid_argument("Option outside supported limits");
  }
  if (options.connect && !options.map.empty()) {
    throw std::invalid_argument("Choose a live connection or an offline map");
  }
  if (!options.stress.empty() &&
      (options.connect || !options.map.empty() || options.stress_summary.empty() ||
       options.frames < 1 || options.stress_delay < 0 || options.stress_delay > 100)) {
    throw std::invalid_argument(
        "--stress needs finite --frames/--stress-summary and offline fixture mode");
  }
  if (options.fixture_step && (options.connect || options.frames < 1)) {
    throw std::invalid_argument("Fixture clock requires finite offline frames");
  }
  if (options.stress_delay < 0 || options.stress_delay > 100) {
    throw std::invalid_argument("Fixture delay outside bounds");
  }
  options.world = true;

  if (options.benchmark && options.frames == 0) {
    options.frames = 600;
  }

  if (options.hidden && options.frames == 0 && options.script.empty()) {
    throw std::invalid_argument("--hidden requires a finite --frames count or a --script");
  }
  return options;
}
}
