#include "application_options.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace realm::application {
// Integer environment override, validated like a command-line value.
int environment_integer(const char* name, int fallback, int low, int high) {
    const char* value = std::getenv(name);
    if (!value) return fallback;
    const std::string_view text(value); int parsed{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() || parsed < low || parsed > high) throw std::invalid_argument(std::string("Invalid ") + name);
    return parsed;
}
// Dynamic atlas layers for composed bitmaps (2048x2048 RGBA = 16 MiB each). REALM_DYNAMIC_ATLAS_PAGES is the maximum (default 12, 0
// disables); the atlas starts at REALM_DYNAMIC_ATLAS_INITIAL (default 2, clamped to [1, maximum]) and grows on demand.
int dynamic_atlas_maximum() { return environment_integer("REALM_DYNAMIC_ATLAS_PAGES", 12, 0, 64); }
int dynamic_atlas_initial(int maximum) { return maximum ? std::clamp(environment_integer("REALM_DYNAMIC_ATLAS_INITIAL", 2, 1, 64), 1, maximum) : 0; }

int integer(std::string_view value) {
    int result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) throw std::invalid_argument("Invalid integer: " + std::string(value));
    return result;
}
Options parse(int argc, char** argv, bool client_default) {
    Options o; o.client = client_default;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string { if (++i == argc) throw std::invalid_argument("Missing value for " + arg); return argv[i]; };
        if (arg == "--flash-root") o.root = value();
        else if (arg == "--client") o.client = true;
        else if (arg == "--server") o.session.host = value();
        else if (arg == "--app-port") { const auto port = integer(value()); if (port < 1 || port > 65535) throw std::invalid_argument("Invalid app port"); o.app_port = static_cast<std::uint16_t>(port); }
        else if (arg == "--account-file") o.account_file = value();
        else if (arg == "--settings-file") o.settings_file = value();
        else if (arg == "--script") o.script = value();
        else if (arg == "--connect") { o.session.host = value(); o.connect = o.world = true; }
        else if (arg == "--port") { const auto port = integer(value()); if (port < 1 || port > 65535) throw std::invalid_argument("Invalid port"); o.session.port = static_cast<std::uint16_t>(port); }
        else if (arg == "--username") o.session.username = value();
        else if (arg == "--password-stdin") o.password_stdin = true;
        else if (arg == "--character") o.session.character_id = integer(value());
        else if (arg == "--create") { const auto type = integer(value()); if (type < 0 || type > 65535) throw std::invalid_argument("Invalid class type"); o.session.create_character = true; o.session.class_type = static_cast<std::uint16_t>(type); }
        else if (arg == "--game") o.session.game_id = integer(value());
        else if (arg == "--projectile-angle") { o.projectile_angle=std::stod(value()); o.projectile_fixture=true; }
        else if (arg == "--projectile-size") o.projectile_size=integer(value());
        else if (arg == "--projectile-outline") o.projectile_outline=true;
        else if (arg == "--raw-flash-bitmap") o.raw_flash_bitmap=true;
        else if (arg == "--fonts") o.fonts=value();
        else if (arg == "--hud-art") o.hud_art=value();
        else if (arg == "--text") o.text=value();
        else if (arg == "--font-size") o.font_size=integer(value());
        else if (arg == "--font-bold") o.font_bold=true;
        else if (arg == "--font-shadow") o.font_shadow=true;
        else if (arg == "--font-yhalf") o.font_yhalf=true;
        else if (arg == "--texture") o.texture = value();
        else if (arg == "--processed") o.processed = value();
        else if (arg == "--map") { o.map = value(); o.world = true; }
        else if (arg == "--sheet") o.sheet = value();
        else if (arg == "--width") o.width = integer(value());
        else if (arg == "--height") o.height = integer(value());
        else if (arg == "--frames") o.frames = integer(value());
        else if (arg == "--sprites") o.sprites = integer(value());
        else if (arg == "--scale") o.scale = integer(value());
        else if (arg == "--stress") { o.stress=value();o.world=true; }
        else if (arg == "--stress-summary") o.stress_summary=value();
        else if (arg == "--fixture-step-ms") { o.fixture_step=integer(value());if(o.fixture_step!=20)throw std::invalid_argument("Fixture clock uses 20ms steps"); }
        else if (arg == "--stress-delay") o.stress_delay=integer(value());
        else if (arg == "--telemetry") o.telemetry=value();
        else if (arg == "--trace") o.trace=value();
        else if (arg == "--telemetry-warmup") { o.telemetry_warmup=integer(value());if(o.telemetry_warmup<0)throw std::invalid_argument("Invalid telemetry warmup"); }
        else if (arg == "--capture") o.capture = value();
        else if (arg == "--condition") o.condition = static_cast<std::uint32_t>(std::stoul(value()));
        else if (arg == "--breath") o.breath = integer(value());
        else if (arg == "--hp") o.hp = integer(value());
        else if (arg == "--scene-time") o.scene_time = integer(value());
        else if (arg == "--ghost") o.ghost = true;
        else if (arg == "--ally") o.ally = true;
        else if (arg == "--ally-name") o.ally_name = value();
        else if (arg == "--notify") o.notify = value();
        else if (arg == "--damage") o.damage = integer(value());
        else if (arg == "--damage-effects") o.damage_effects = value();
        else if (arg == "--status-age") o.status_age = integer(value());
        else if (arg == "--background") o.background = integer(value());
        else if (arg == "--effect-time") o.effect_time = integer(value());
        else if (arg == "--hidden") o.hidden = true;
        else if (arg == "--benchmark") o.benchmark = true;
        else if (arg == "--world") o.world = true;
        else if (arg == "--offset") o.offset = true;
        else if (arg == "--help") {
            std::cout << "realm_viewer [--flash-root PATH] [--sheet NAME] [--width N] [--height N]\n"
                "  [--scale N] [--map PATH.jm|PATH.wmap] [--world] [--offset] [--frames N] [--sprites N]\n"
                "  [--processed DIR] [--fonts DIR] [--hud-art DIR] [--capture PATH.png] [--hidden] [--benchmark]\n"
                "  [--telemetry PATH.jsonl] [--trace PATH.json] [--telemetry-warmup N] (frame count)\n"
                "  [--stress MANIFEST.json --stress-summary RESULT.json --frames N] [--stress-delay MS]\n"
                "Gallery: Left/Right select set; Up/Down page. Tab toggles camera fixture.\n"
                "Camera fixture: WASD pan; Q/E rotate (with Allow Camera Rotation); +/- zoom; C toggle camera centering. Escape exits.\n"
                "Live session: --connect HOST --port N --username NAME --password-stdin --character N\n"
                "Use --create CLASS_TYPE to create a character instead of loading one.\n"
                "Inventory: drag to swap/drop; double-click equip/use; Shift-click use; Ctrl-click backpack transfer; 1-8 use.\n"
                "Potions: F/V or Shift/double-click the health/magic stacks.\n"
                "Portals: click Enter or press 0 beside a portal.\n"
                "Options: O opens the Options screen (key bindings and settings). --settings-file PATH stores them\n"
                "  (default beside the account file; the harness keeps the defaults unless a file is given).\n"
                "Client: --client [--server HOST] [--port N] [--app-port N] [--account-file PATH] runs the\n"
                "  startup sequence: loading, title, character selection, then gameplay (realm_client default).\n"
                "  Escape in game returns to character selection.\n"
                "Input script: --script FILE replays lines 'MS click X Y', 'MS key escape|enter|tab|backspace',\n"
                "  'MS hold KEY' / 'MS release KEY' (a held key for polled controls),\n"
                "  'MS move X Y', 'MS text STRING', 'MS capture PATH.png' and 'MS quit' (MS since start, X/Y window points).\n";
            std::exit(0);
        } else throw std::invalid_argument("Unknown option: " + arg);
    }
    if (o.width < 64 || o.width > 8192 || o.height < 64 || o.height > 8192 ||
        o.frames < 0 || o.sprites < 0 || o.sprites > 900000 || o.scale < 1 || o.scale > 32)
        throw std::invalid_argument("Option outside supported limits");
    if (!std::isfinite(o.projectile_angle) || o.projectile_size<1 || o.projectile_size>2000) throw std::invalid_argument("Invalid projectile fixture");
    if (o.connect && !o.map.empty()) throw std::invalid_argument("Choose a live connection or an offline map");
    if (o.client && (o.connect || !o.map.empty() || !o.texture.empty() || !o.text.empty()))
        throw std::invalid_argument("The client startup sequence cannot be combined with --connect, --map, --texture or --text");
    if (!o.stress.empty() && (o.client||o.connect||!o.map.empty()||o.stress_summary.empty()||o.frames<1||o.stress_delay<0||o.stress_delay>100))throw std::invalid_argument("--stress needs finite --frames/--stress-summary and offline fixture mode");
    if(o.fixture_step&&(o.connect||o.frames<1))throw std::invalid_argument("Fixture clock requires finite offline frames");
    if(o.stress_delay<0||o.stress_delay>100)throw std::invalid_argument("Fixture delay outside bounds");
    if (o.client) o.world = true;
    if (o.benchmark && o.frames == 0) o.frames = 600;
    if (o.hidden && o.frames == 0 && o.script.empty()) throw std::invalid_argument("--hidden requires a finite --frames count or a --script");
    return o;
}
}
