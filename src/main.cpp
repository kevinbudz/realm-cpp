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
#include "realm/player_group.hpp"
#include "realm/party_arrows.hpp"
#include "realm/startup.hpp"
#include "realm/text_tooltip.hpp"
#include "realm/options.hpp"
#include "realm/sdl_keys.hpp"
#include "realm/settings.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <deque>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <sstream>
#include <string>
#include <set>
#ifdef __linux__
#include <unistd.h>
#endif

#ifndef REALM_CLIENT_DEFAULT
#define REALM_CLIENT_DEFAULT 0
#endif
namespace {
using Clock = std::chrono::steady_clock;
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
struct Options {
    std::filesystem::path root{realm::flash::source_root}, capture, map, processed, texture, fonts, hud_art, account_file, settings_file, script;
    bool client{REALM_CLIENT_DEFAULT != 0};
    std::uint16_t app_port{7777};
    std::string text;int font_size{14};bool font_bold{},font_shadow{},font_yhalf{};
    std::string sheet{"lofiEnvironment"};
    int width{800}, height{600}, frames{}, sprites{}, scale{4};
    bool hidden{}, benchmark{}, world{}, offset{}, connect{}, password_stdin{}, projectile_fixture{}, projectile_outline{};
    double projectile_angle{}; int projectile_size{100}; bool raw_flash_bitmap{};
    // Fixture inputs for the post effects (--map only): player condition bits, breath stat, and a fixed effect clock.
    std::uint32_t condition{}; int breath{-1}, background{}; std::int32_t effect_time{-1};
    // Status fixtures (--map only): health, a second player with a name, floating text and a simulated hit.
    int hp{-1}, damage{}, status_age{}; std::int32_t scene_time{-1}; bool ghost{}; bool ally{}; std::string ally_name, notify, damage_effects;
    std::filesystem::path telemetry, trace, stress, stress_summary;
    int stress_delay{}, fixture_step{};
    int telemetry_warmup{30};
    realm::SessionOptions session;
};
int integer(std::string_view value) {
    int result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) throw std::invalid_argument("Invalid integer: " + std::string(value));
    return result;
}
Options parse(int argc, char** argv) {
    Options o;
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
double mouse_wheel_steps(const SDL_MouseWheelEvent& event) {
    if(event.y)return event.y;
#if SDL_VERSION_ATLEAST(2,0,18)
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
struct ScriptStep { double time{}; std::string action, argument; int x{}, y{}; };
std::vector<ScriptStep> load_script(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot read script " + path.string());
    std::vector<ScriptStep> steps; std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line); ScriptStep step;
        if (!(in >> step.time >> step.action)) throw std::invalid_argument("Invalid script line: " + line);
        if (step.action == "click" || step.action == "move" || step.action == "resize" || step.action == "down" || step.action == "up") { if (!(in >> step.x >> step.y)) throw std::invalid_argument("Invalid pointer input: " + line); }
        else if(step.action=="wheel") { if(!(in>>step.y))throw std::invalid_argument("Invalid wheel input: "+line); }
        else if (step.action == "key" || step.action == "hold" || step.action == "release" || step.action == "text" || step.action == "capture") {
            std::getline(in >> std::ws, step.argument);
            if (step.argument.empty()) throw std::invalid_argument("Invalid script argument: " + line);
        } else if (step.action != "quit" && step.action != "ability-down" && step.action != "ability-up") throw std::invalid_argument("Unknown script action: " + line);
        steps.push_back(std::move(step));
    }
    std::stable_sort(steps.begin(), steps.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    return steps;
}
// Options.refreshCursor: cursorsEmbed is a sheet of 32x32 cursors with the hot spot at (15, 15);
// "OS" (-1) restores the system cursor. Cursors are created on first use.
class CursorSet {
public:
    explicit CursorSet(const std::filesystem::path& flash_root) {
        const auto path = flash_root / "src" / "kabam" / "rotmg" / "assets" / "EmbeddedAssets_cursorsEmbed_.png";
        if (std::filesystem::exists(path)) sheet_ = realm::load_png(path);
    }
    CursorSet(const CursorSet&) = delete;
    CursorSet& operator=(const CursorSet&) = delete;
    ~CursorSet() { SDL_SetCursor(SDL_GetDefaultCursor()); for (auto* cursor : cursors_) if (cursor) SDL_FreeCursor(cursor); }
    void select(int index) {
        if (index == shown_) return;
        shown_ = index;
        if (index < 0 || index >= 16 || sheet_.width < 128 || sheet_.height < 128) { SDL_SetCursor(SDL_GetDefaultCursor()); return; }
        auto& cursor = cursors_[static_cast<std::size_t>(index)];
        if (!cursor) {
            std::vector<unsigned char> pixels(32 * 32 * 4);
            const int left = (index % 4) * 32, top = (index / 4) * 32;
            for (int y = 0; y < 32; ++y)
                std::copy_n(sheet_.pixels.data() + (static_cast<std::size_t>(top + y) * static_cast<std::size_t>(sheet_.width) + static_cast<std::size_t>(left)) * 4,
                            32 * 4, pixels.data() + static_cast<std::size_t>(y) * 32 * 4);
            if (auto* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), 32, 32, 32, 32 * 4, SDL_PIXELFORMAT_RGBA32)) {
                cursor = SDL_CreateColorCursor(surface, 15, 15);
                SDL_FreeSurface(surface);
            }
        }
        SDL_SetCursor(cursor ? cursor : SDL_GetDefaultCursor());
    }
private:
    realm::Image sheet_;
    std::array<SDL_Cursor*, 16> cursors_{};
    int shown_{-2};
};
// Resident memory for the performance panel (Linux /proc; other platforms report 0).
double resident_megabytes() {
#ifdef __linux__
    std::ifstream statm("/proc/self/statm");
    long long size{}, resident{};
    if (statm >> size >> resident) return static_cast<double>(resident) * static_cast<double>(sysconf(_SC_PAGESIZE)) / 1048576.0;
#endif
    return 0;
}
struct SDLSession {
    SDL_Window* window{};
    SDL_GLContext context{};
    ~SDLSession() { if (context) SDL_GL_DeleteContext(context); if (window) SDL_DestroyWindow(window); SDL_Quit(); }
};
}

int main(int argc, char** argv) {
    try {
        auto o = parse(argc, argv);
        // Parameters.data_: the harness keeps Flash's defaults unless a settings or account file is named, so
        // fixtures never pick up the player's bindings.
        const bool persist_settings = o.client || !o.settings_file.empty() || !o.account_file.empty();
        const auto settings_path = !o.settings_file.empty() ? o.settings_file : realm::Settings::path_beside(o.account_file);
        realm::Settings settings = persist_settings ? realm::Settings::load(settings_path) : realm::Settings{};
        if (o.client) {
            // Packaged client assets sit beside the executable (build/fonts-client, build/hud-art).
            std::filesystem::path base;
            if (char* path = SDL_GetBasePath()) { base = path; SDL_free(path); }
            const auto find = [&](std::filesystem::path& option, std::string_view name, std::string_view tool) {
                if (!option.empty()) return;
                for (const auto& candidate : {base / name, std::filesystem::path("build") / name})
                    if (std::filesystem::is_directory(candidate)) { option = candidate; return; }
                throw std::runtime_error("Client assets missing: " + std::string(name) + " (run " + std::string(tool) + " or pass the directory)");
            };
            find(o.fonts, "fonts-client", "tools/bake_fonts.py --output build/fonts-client --reference-shadows, or pass --fonts");
            find(o.hud_art, "hud-art", "tools/bake_hud_art.py --output build/hud-art, or pass --hud-art");
        }
        SDL_SetMainReady(); SDLSession session;
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8); SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8); SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
        session.window = SDL_CreateWindow(o.client ? "Realm of the Mad God" : "Realm C++ rendering harness", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            o.width, o.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (o.hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN));
        if (!session.window) throw std::runtime_error(SDL_GetError());
        session.context = SDL_GL_CreateContext(session.window);
        if (!session.context) throw std::runtime_error(SDL_GetError());
        glewExperimental = GL_TRUE;
        const auto glew_status = glewInit();
        if (glew_status != GLEW_OK) throw std::runtime_error(reinterpret_cast<const char*>(glewGetErrorString(glew_status)));
        // GLEW can leave GL_INVALID_ENUM on core contexts during extension discovery.
        while (glGetError() != GL_NO_ERROR) {}
        if (!GLEW_VERSION_3_3) throw std::runtime_error("OpenGL 3.3 required");
        if (SDL_GL_SetSwapInterval(o.benchmark ? 0 : settings.flag("vsync") ? 1 : 0) != 0)
            std::cerr << "Swap interval unavailable: " << SDL_GetError() << '\n';
        std::cout << "Flash revision " << realm::flash::revision << "\nGPU " << glGetString(GL_RENDERER)
                  << "\nGL " << glGetString(GL_VERSION) << '\n';
        realm::AnimationLibrary animations(o.root);
        realm::ProcessedTextures processed(o.processed, animations.images().size());
        auto atlas_images = animations.images();
        atlas_images.insert(atlas_images.end(), processed.images().begin(), processed.images().end());
        realm::Catalog catalog(o.root);realm::Appearances appearances(o.root,animations);realm::FontBook fonts(o.fonts);
        if(!fonts.empty())fonts.enable_native(o.root);
        realm::SurfaceTexture hud_texture,minimap_texture,tooltip_texture,player_tooltip_texture,player_menu_texture,chat_texture,trade_texture,trade_request_texture,interaction_texture,startup_texture,dialog_texture,board_texture,members_texture,group_texture,skin_texture,options_texture,perf_texture,header_tip_texture;
        std::unique_ptr<realm::HudArt> hud_art;if(!o.hud_art.empty())hud_art=std::make_unique<realm::HudArt>(o.hud_art);
        if(hud_art)hud_art->appearances(&appearances,&catalog);
        std::unique_ptr<realm::Hud> hud;
        if(o.world&&!fonts.empty()) { hud=std::make_unique<realm::Hud>(fonts,hud_art.get(),&catalog); }
        std::unique_ptr<realm::Minimap> minimap;
        if(hud) { minimap=std::make_unique<realm::Minimap>(catalog,animations,o.root); }
        const auto drag_index=atlas_images.size();realm::Image drag_bitmap{128,128,std::vector<unsigned char>(128*128*4)};
        if(hud_art)atlas_images.push_back(drag_bitmap);
        std::unique_ptr<realm::ItemTooltip> tooltip;
        if(hud&&hud_art) { tooltip=std::make_unique<realm::ItemTooltip>(catalog,fonts,*hud_art); }
        realm::PartyHover party_hover;
        std::unique_ptr<realm::PlayerTooltip> player_tooltip;
        if(hud&&hud_art) { player_tooltip=std::make_unique<realm::PlayerTooltip>(catalog,fonts,*hud_art); }
        std::unique_ptr<realm::PlayerMenu> player_menu;
        if(hud&&hud_art&&hud_art->contains("menuframe")) { player_menu=std::make_unique<realm::PlayerMenu>(catalog,fonts,*hud_art); }
        std::unique_ptr<realm::PlayerGroup> player_group;
        if(hud&&hud_art)player_group=std::make_unique<realm::PlayerGroup>(catalog,fonts,*hud_art);
        std::unique_ptr<realm::SkinChooser> skins;if(hud&&hud_art)skins=std::make_unique<realm::SkinChooser>(catalog,fonts,*hud_art);
        bool skin_press_guard{};
        bool header_pressed{};std::string header_tip_key;realm::TextToolTipImage header_tip;
        bool player_menu_pressed{},group_pressed{},minimap_pressed{},arrow_pressed{};
        realm::PartyArrows party_arrows;
        std::array<realm::SurfaceTexture,realm::Party::members_shown> arrow_textures;
        std::array<double,realm::Party::members_shown> arrow_rotations{},arrow_densities{};
        std::array<bool,realm::Party::members_shown> arrow_hovers{};
        // TextBox lines persist for the whole run, across maps and characters.
        realm::Chat chat;std::unique_ptr<realm::ChatView> chat_view;
        realm::SpeechBalloons balloons;std::array<realm::SurfaceTexture,realm::SpeechBalloons::capacity> balloon_textures;
        std::array<std::uint64_t,realm::SpeechBalloons::capacity> balloon_uploaded{};
        realm::Trade trade;std::unique_ptr<realm::TradeView> trade_view;int trade_pressed{-1};
        if(hud&&hud_art&&hud_art->contains("tradefill0"))trade_view=std::make_unique<realm::TradeView>(fonts,*hud_art);
        if(hud)chat_view=std::make_unique<realm::ChatView>(fonts,hud_art.get());
        realm::OptionsScreen options_screen;CursorSet cursors(o.root);realm::InteractionDialog interaction;realm::Dialog dialog;realm::GuildBoard board;realm::GuildMembers members(o.hud_art/"guild-members");
        bool tooltip_pointer_valid{},hud_mouse_down{};
        realm::HudInput hud_input;std::string drag_key;std::int32_t hud_container=-1;
        // The interaction panel as last drawn; clicks hit what is on screen.
        realm::HudState hud_view;
        realm::TextBitmap text_fixture;
        const auto text_fixture_index=atlas_images.size();
        if(!o.text.empty()) { text_fixture=fonts.rasterize(o.text,o.font_size,o.font_bold,o.font_shadow,0xffffff,0,o.font_yhalf?.5:0);atlas_images.push_back(text_fixture.image); }
        realm::World world(catalog); realm::Scene scene;realm::StatusArt status_art(fonts,&appearances);realm::StatusTexts status_texts;scene.set_status_art(&status_art); realm::Backdrop backdrop; std::uint64_t backdrop_generation{~0ULL};
        const auto own_name=[&] { const auto* player=world.entity(world.player_id());return player ? player->name : std::string{}; };
        std::unique_ptr<realm::Session> game_session;
        std::unique_ptr<realm::Startup> startup;
        // World holds MapBackground's map until a session's MAPINFO replaces it.
        bool background_world{};
        if (o.client) {
            if (fonts.empty()) throw std::runtime_error("The client needs baked fonts (--fonts)");
            startup = std::make_unique<realm::Startup>(fonts, hud_art.get(), catalog, o.root,
                realm::StartupOptions{o.session.host, o.session.build_version, o.app_port, o.session.port, o.account_file});
            startup->load_background(world); background_world = true;
        }
        if (!o.map.empty()) {
            if (o.map.extension() == ".wmap") world.load_wmap(o.map); else world.load_jm(o.map);
            const auto spawn = world.spawn_position();
            if (const auto* player = catalog.object("Wizard")) { world.add(-2, player->type, spawn); world.set_player_id(-2); auto* local = world.entity(-2); local->name="Wizard";local->condition=o.condition;if(o.breath>=0)local->stats[static_cast<std::uint8_t>(realm::flash::StatId::BREATH_STAT)]=o.breath;local->stats[static_cast<std::uint8_t>(realm::flash::StatId::MP_STAT)]=player->max_mp;local->stats[static_cast<std::uint8_t>(realm::flash::StatId::MAX_MP_STAT)]=player->max_mp;local->stats[static_cast<std::uint8_t>(realm::flash::StatId::NEXT_LEVEL_EXP_STAT)]=50; local->speed = player->xml.integer("Speed"); local->attack = player->xml.integer("Attack"); local->dexterity = player->xml.integer("Dexterity");
                auto equipment=player->xml.child("Equipment").text();std::replace(equipment.begin(),equipment.end(),',',' ');std::istringstream slots(equipment);std::string item;int slot{};while(slot<20&&slots>>item)realm::set_inventory_item(*local,slot++,{std::stoi(item,nullptr,0),-1}); }
            if (auto* local = world.entity(-2)) {
                local->max_hp = 100; local->hp = o.hp >= 0 ? o.hp : 100;
                if (o.ally) {
                    auto& ally = world.add(-3, local->type, {local->position.x + 2, local->position.y});
                    ally.name = o.ally_name; ally.max_hp = 100; ally.hp = o.hp >= 0 ? o.hp : 100; ally.condition = o.condition;
                }
                if (!o.notify.empty()) {
                    // TEXT[:RRGGBB], two seconds above the local player.
                    const auto colon = o.notify.rfind(':');
                    const bool colored = colon != std::string::npos && o.notify.size() - colon == 7;
                    world.notify(-2, colored ? o.notify.substr(0, colon) : o.notify, colored ? static_cast<std::uint32_t>(std::stoul(o.notify.substr(colon + 1), nullptr, 16)) : 0xffffffU, 20000);
                }
                if (o.damage > 0 || !o.damage_effects.empty()) {
                    std::vector<std::uint8_t> effects; std::istringstream list(o.damage_effects); std::string part;
                    while (std::getline(list, part, ',')) effects.push_back(static_cast<std::uint8_t>(std::stoi(part)));
                    world.report_damage(-2, o.damage, 0, effects);
                }
            }
            // Background reference for status fixtures: the same scene without the player's sprite and bars.
            if (o.ghost) world.remove(-2);
            std::cout << "Map " << world.name() << " " << world.width() << "x" << world.height() << " objects=" << world.entities().size() << " unresolved=" << world.unresolved_ids().size() << '\n';
        }
        std::unique_ptr<realm::quality::StressTimeline> stress;
        if(!o.stress.empty())stress=std::make_unique<realm::quality::StressTimeline>(world,realm::quality::StressConfig::load(o.stress));
        std::unique_ptr<realm::Terrain> terrain;
        const auto terrain_first = atlas_images.size();
        if (!o.map.empty() || o.connect || o.client || stress) {
            terrain = std::make_unique<realm::Terrain>(world, o.root, terrain_first, -1, o.connect || o.client || static_cast<bool>(stress));
            atlas_images.insert(atlas_images.end(), terrain->images().begin(), terrain->images().end());
            std::cout << "Terrain variants=" << terrain->images().size() << '\n';
        }
        const auto fixture_index = atlas_images.size();
        if (!o.texture.empty()) { auto image=realm::load_png(o.texture); if(o.raw_flash_bitmap) realm::normalize_flash_png(image); atlas_images.push_back(std::move(image)); }
        const int atlas_maximum = dynamic_atlas_maximum(), atlas_initial = dynamic_atlas_initial(atlas_maximum);
        realm::Atlas atlas(o.root, atlas_images, animations.images().size(), atlas_initial);
        scene.set_dynamic_atlas_pages(atlas_initial, atlas_maximum, static_cast<std::uint64_t>(environment_integer("REALM_ATLAS_GROW_FRAMES", 120, 1, 100000)));
        // REALM_DYNAMIC_ATLAS=0 is the differential oracle: the same atlas, but every bitmap takes the per-image SurfaceTexture path.
        if (const char* flag = std::getenv("REALM_DYNAMIC_ATLAS"); flag && std::string_view(flag) == "0") scene.set_dynamic_atlas(false);
        scene.set_atlas_protect_frames(static_cast<std::uint64_t>(environment_integer("REALM_ATLAS_PROTECT_FRAMES", 2, 1, 64)));
        const std::string app_host=o.session.host;
        if (o.connect) {
            if (o.password_stdin) std::getline(std::cin,o.session.password);
            game_session = std::make_unique<realm::Session>(world); game_session->connect(std::move(o.session));
        }
        constexpr std::size_t base_capacity = 4096;
        if(!o.trace.empty()&&o.telemetry.empty())throw std::invalid_argument("--trace requires --telemetry");
        if(!o.trace.empty()&&std::filesystem::absolute(o.trace).lexically_normal()==std::filesystem::absolute(o.telemetry).lexically_normal())throw std::invalid_argument("Trace and sample paths must differ");
        std::unique_ptr<realm::telemetry::Recorder> telemetry;
        if(!o.telemetry.empty()) {
            telemetry=std::make_unique<realm::telemetry::Recorder>(o.telemetry,o.trace);
            if(stress){telemetry->aggregate_repeated(true);telemetry->metadata("zone_mode","stress aggregate: cumulative inclusive duration per name anchored at first occurrence, not continuous spans");telemetry->metadata("manifest",o.stress.string());}
            telemetry->metadata("GL_vendor",reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
            telemetry->metadata("GL_renderer",reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
            telemetry->metadata("GL_version",reinterpret_cast<const char*>(glGetString(GL_VERSION)));
            telemetry->metadata("workload",!o.map.empty()?o.map.string():o.client?"client":"gallery/camera/extra-quads");
            telemetry->metadata("window",o.hidden?"hidden":"visible");
        }
        realm::Renderer renderer(base_capacity + static_cast<std::size_t>(o.sprites));
        std::cout << "Atlas pages=" << atlas.pages() << " bytes=" << atlas.bytes() << " PNGs=" << realm::flash::assets.size() << '\n';
        std::size_t selected{};
        const auto it = std::find_if(realm::flash::sheets.begin(), realm::flash::sheets.end(),
                                    [&](const auto& s) { return s.name == o.sheet; });
        if (it == realm::flash::sheets.end()) throw std::invalid_argument("Unknown image set: " + o.sheet);
        selected = static_cast<std::size_t>(it - realm::flash::sheets.begin());
        std::vector<realm::Quad> quads; quads.reserve(base_capacity + static_cast<std::size_t>(o.sprites));
        std::vector<double> cpu_samples, present_samples;
        if (o.frames) { cpu_samples.reserve(static_cast<std::size_t>(o.frames)); present_samples.reserve(static_cast<std::size_t>(o.frames)); }
        realm::Camera camera; realm::Vec2 position = o.map.empty()&&!stress ? realm::Vec2{} : world.spawn_position();
        double angle = settings.camera_angle(), zoom = 1;
        bool centered = !o.offset && settings.flag("centerOnPlayer"), running = true, leaving_game = false;
        bool applied_centering = settings.flag("centerOnPlayer"), autofire = false, perf_stats = false;
        // Applies Parameters.data_ to the systems that read it: world flags, VSync, window mode and cursor.
        const auto apply_settings = [&] {
            world.set_preferences(settings.preferences());
            if (const bool center = settings.flag("centerOnPlayer"); center != applied_centering) { applied_centering = center; centered = center; }
            if (o.benchmark) return;
            SDL_GL_SetSwapInterval(settings.flag("vsync") ? 1 : 0);
            const bool fullscreen = (SDL_GetWindowFlags(session.window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
            if (!o.hidden && settings.flag("fullscreenMode") != fullscreen) SDL_SetWindowFullscreen(session.window, settings.flag("fullscreenMode") ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            if (!o.hidden && persist_settings) cursors.select(settings.get("cursor"));
        };
        const auto save_settings = [&] {
            settings.set_camera_angle(angle);
            if (persist_settings && !settings.save(settings_path)) std::cerr << "Cannot write settings " << settings_path << '\n';
        };
        apply_settings();
        // MapUserInput.clearInput, then the Options screen (the key, or CharacterDetailsView's button).
        const auto open_options = [&] {
            hud_input.cancel(); hud_mouse_down = false; tooltip_pointer_valid = false;
            // Options.onAddedToStage reads the window's real mode back into fullscreenMode.
            const bool fullscreen = (SDL_GetWindowFlags(session.window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
            if (settings.set_choice("fullscreenMode", fullscreen ? 1 : 0)) save_settings();
            options_screen.open(settings);
        };
        std::set<SDL_Scancode> script_keys;
        int perf_frames{};double perf_clock{},perf_cpu{},perf_worst{};std::string perf_text;bool perf_dirty{};realm::Vec2 perf_size{};
        std::vector<realm::Quad> menu_quads;
        const auto script = o.script.empty() ? std::vector<ScriptStep>{} : load_script(o.script);
        std::size_t script_next{};
        bool script_ability_down{};bool script_mouse_down{};realm::Vec2 script_pointer{};
        int page{}; std::uint64_t frame{}; auto previous = Clock::now(); const auto epoch = previous;
        bool previous_capture{};
        while (running && (!o.frames || frame < static_cast<std::uint64_t>(o.frames))) {
            const auto start = Clock::now();
            const double frame_interval=milliseconds(start-previous);
            if(telemetry)telemetry->begin(frame,start);
            realm::telemetry::Zone cpu_zone("frame.cpu");
            double input_age=-60000;bool input_observed{};
            const auto previous_drops=world.particles().dropped();
            const double dt = stress ? stress->config().step_ms/1000.0 : o.fixture_step ? o.fixture_step/1000.0 : std::min(frame_interval / 1000, .1); previous = start;
            const auto time = stress ? stress->time() : o.fixture_step ? static_cast<std::int32_t>(frame*static_cast<std::uint64_t>(o.fixture_step)) : static_cast<std::int32_t>(milliseconds(start - epoch));
            if(stress) {
                stress->step();position=stress->camera_position();angle=stress->camera_angle();zoom=stress->zoom_factor();
                const auto& kind=stress->config().workload;
                if(kind=="UI"||kind=="MIXED") {
                    REALM_ZONE("fixture.ui_input");
                    const auto phase=frame%100;
                    if(phase==0){chat.add("Fixture","Crowded combat and chat scroll",time);chat.open();}
                    if(phase<20)chat.text("x");
                    if(phase==20){(void)chat.submit();chat.scroll_up();}
                    if(phase==25)chat.scroll_down();
                    if(phase==40)options_screen.open(settings);
                    if(phase==45)options_screen.set_tab(realm::OptionsTab::graphics);
                    if(phase==60)options_screen.close();
                    if(phase==70)(void)dialog.purchase_result({1,"Fixture error"});
                    if(phase==80)dialog.close();
                    tooltip_pointer_valid=phase>=85;hud_input.move({20,320},hud_input.selected());
                    if(const auto* p=world.entity(world.player_id())){const auto inventory=realm::HudState::player(world,*p).inventory;
                        if(phase==85)hud_input.down({20,320},inventory);
                        if(phase==86){hud_input.move({26,320},hud_input.selected());if(hud_input.dragging()!=0)throw std::runtime_error("Stress UI failed to drag equipped weapon");}
                        if(phase==88)hud_input.move({70,430},hud_input.selected());
                        if(phase==90)(void)hud_input.up({70,430},false,false,time,inventory);}
                }
            }
            if (terrain) terrain->begin_frame();
            if (startup) if (auto launch = startup->take_launch()) {
                game_session = std::make_unique<realm::Session>(world); background_world = false;
                try { game_session->connect(std::move(*launch)); }
                catch (const std::exception& e) { game_session.reset(); startup->left_game(e.what()); startup->load_background(world); background_world = true; }
            }
            if (game_session) {
                game_session->poll(time); world.update(dt*1000);
                for (const auto& message : game_session->take_events()) {
                    if(message.id==realm::flash::PacketId::MAPINFO||message.id==realm::flash::PacketId::RECONNECT||message.id==realm::flash::PacketId::DEATH) {
                        dialog.close();board.close();members.close();options_screen.close();if(skins)skins->close();party_hover.reset();hud_view.party={};tooltip_pointer_valid=false;
                        if(player_menu) { player_menu->close(); } if(player_group)player_group->close();group_pressed=minimap_pressed=arrow_pressed=false;party_arrows.reset();
                        player_menu_pressed=false;chat.close();balloons.clear();status_texts.clear();trade.end();trade_pressed=-1;
                    }
                    interaction.receive(message);
                    if(auto name=interaction.take_name();name&&startup)startup->renamed_account(*name);
                    // onBuyResult: result 1 opens a "Purchase Error" dialog in Flash; other
                    // results become chat lines (server color on success, otherwise *Error*).
                    if(message.id==realm::flash::PacketId::BUYRESULT) {
                        const auto& result=std::get<realm::net::incoming::BuyResult>(message.data);
                        if(dialog.purchase_result(result)) {
                            chat.close();hud_input.cancel();hud_mouse_down=false;trade_pressed=-1;party_hover.reset();tooltip_pointer_valid=false;
                            if(player_menu) { player_menu->close(); } if(player_group)player_group->close();group_pressed=minimap_pressed=arrow_pressed=false;party_arrows.reset();
                            player_menu_pressed=false;
                        } else chat.add(std::string(result.result==0 ? realm::server_chat_name : realm::error_chat_name),result.text,time);
                    }
                    switch(message.id) {
                    case realm::flash::PacketId::TRADEREQUESTED: {
                        const auto& name=std::get<realm::net::incoming::TradeRequested>(message.data).name;
                        if(trade_view&&!trade.active()&&settings.flag("showTradePopup"))trade.requested(name,time);
                        chat.add("",name+" wants to trade with you.  Type \"/trade "+name+"\" to trade.",time);break;
                    }
                    case realm::flash::PacketId::TRADESTART: trade.start(std::get<realm::net::incoming::TradeStart>(message.data),own_name(),time);trade_pressed=-1;break;
                    case realm::flash::PacketId::TRADECHANGED: trade.changed(std::get<realm::net::incoming::TradeChanged>(message.data),time);break;
                    case realm::flash::PacketId::TRADEACCEPTED: trade.accepted(std::get<realm::net::incoming::TradeAccepted>(message.data));break;
                    case realm::flash::PacketId::TRADEDONE: trade.end();trade_pressed=-1;chat.add("",std::get<realm::net::incoming::TradeDone>(message.data).text,time);break;
                    default: break;
                    }
                    if (message.id == realm::flash::PacketId::TEXT) {
                        const auto& text=std::get<realm::net::incoming::Text>(message.data);
                        members.receive(text,world.player_id());balloons.add(world,text);chat.add(text,own_name(),time);
                    }
                }
                const auto state = game_session->state();
                if (startup) {
                    if (state == realm::SessionState::playing && startup->menus()) startup->entered_game();
                    if (state == realm::SessionState::failed || state == realm::SessionState::disconnected || state == realm::SessionState::dead) {
                        std::string message = state == realm::SessionState::dead ? "Your character has died." : leaving_game ? "" : game_session->error();
                        if (message.empty() && state != realm::SessionState::dead && !leaving_game) message = "Disconnected from the game server.";
                        game_session.reset();interaction.reset();dialog.close();board.close();members.close();options_screen.close();if(skins)skins->close();party_hover.reset();if(player_menu) { player_menu->close(); } if(player_group)player_group->close();group_pressed=minimap_pressed=arrow_pressed=false;party_arrows.reset();player_menu_pressed=false;chat.close();balloons.clear();status_texts.clear();trade.end();trade_pressed=-1;hud_view.party={};tooltip_pointer_valid=false; leaving_game = false; hud_input.cancel(); hud_container = -1;
                        startup->left_game(message); startup->load_background(world); background_world = true;
                    }
                } else if (state == realm::SessionState::failed || state == realm::SessionState::disconnected) throw std::runtime_error(game_session->error());
                interaction.update(world);
                if (game_session) if (const auto* player = world.entity(world.player_id())) position = player->position;
            }
            if(startup)if(auto error=startup->take_skin_error())dialog.message_close("D'oh, this isn't good","An error has occured:\n"+*error,"Ok");
            board.poll();members.poll(dt);if(skins)skins->poll();
            if(members.visible()&&game_session)if(const auto* player=world.entity(world.player_id()))members.permissions(player->guild,(player->stats.contains(static_cast<std::uint8_t>(realm::flash::StatId::GUILD_RANK_STAT)) ? player->stats.at(static_cast<std::uint8_t>(realm::flash::StatId::GUILD_RANK_STAT)) : 0));
            if(auto action=members.take_request();action&&game_session) {
                if(action->action==realm::GuildMembers::Action::remove)game_session->leave_guild(action->name);
                else game_session->change_guild_rank(action->name,action->rank);
            }
            if(board.visible()&&game_session)if(const auto* player=world.entity(world.player_id()))board.permissions((player->stats.contains(static_cast<std::uint8_t>(realm::flash::StatId::GUILD_RANK_STAT)) ? player->stats.at(static_cast<std::uint8_t>(realm::flash::StatId::GUILD_RANK_STAT)) : 0));
            const bool menus = startup && startup->menus();
            if (startup||hud) {
                const bool text = !dialog.visible()&&!options_screen.visible()&&(board.wants_text()||(!board.visible() && !members.visible()&&!(skins&&skins->visible())&&((startup&&startup->wants_text())||interaction.wants_text()||chat.typing())));
                if (text != (SDL_IsTextInputActive() == SDL_TRUE)) { if (text) SDL_StartTextInput(); else SDL_StopTextInput(); }
            }
            std::filesystem::path script_capture;
            for (; script_next < script.size() && script[script_next].time <= time; ++script_next) {
                const auto& step = script[script_next];
                SDL_Event injected{};
                if(step.action=="move"||step.action=="click"||step.action=="down"||step.action=="up")script_pointer={static_cast<double>(step.x),static_cast<double>(step.y)};
                if(step.action=="down")script_mouse_down=true;
                if(step.action=="up"||step.action=="click")script_mouse_down=false;
                if (step.action == "move") {
                    injected.type=SDL_MOUSEMOTION;injected.motion.x=step.x;injected.motion.y=step.y;SDL_PushEvent(&injected);
                } else if (step.action == "click") {
                    injected.type = SDL_MOUSEMOTION; injected.motion.x = step.x; injected.motion.y = step.y; SDL_PushEvent(&injected);
                    injected = {}; injected.type = SDL_MOUSEBUTTONDOWN; injected.button.button = SDL_BUTTON_LEFT; injected.button.state = SDL_PRESSED;
                    injected.button.x = step.x; injected.button.y = step.y; SDL_PushEvent(&injected);
                    injected.type = SDL_MOUSEBUTTONUP; injected.button.state = SDL_RELEASED; SDL_PushEvent(&injected);
                } else if(step.action=="down"||step.action=="up") {
                    injected.type=step.action=="down" ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                    injected.button.button=SDL_BUTTON_LEFT;injected.button.state=step.action=="down" ? SDL_PRESSED : SDL_RELEASED;injected.button.x=step.x;injected.button.y=step.y;SDL_PushEvent(&injected);
                } else if(step.action=="wheel") { injected.type=SDL_MOUSEWHEEL;injected.wheel.y=step.y;
#if SDL_VERSION_ATLEAST(2,0,18)
                    injected.wheel.preciseY=static_cast<float>(step.y);
#endif
                    SDL_PushEvent(&injected);
                } else if(step.action=="ability-down"||step.action=="ability-up") {
                    script_ability_down=step.action=="ability-down";
                    injected.type=script_ability_down ? SDL_KEYDOWN : SDL_KEYUP;injected.key.keysym.sym=SDLK_SPACE;injected.key.keysym.scancode=SDL_SCANCODE_SPACE;SDL_PushEvent(&injected);
                } else if (step.action == "key") {
                    const SDL_Keycode key = SDL_GetKeyFromName(step.argument.c_str());
                    if (key == SDLK_UNKNOWN) throw std::invalid_argument("Unknown script key: " + step.argument);
                    injected.type = SDL_KEYDOWN; injected.key.state = SDL_PRESSED; injected.key.keysym.sym = key; injected.key.keysym.scancode = SDL_GetScancodeFromKey(key);
                    SDL_PushEvent(&injected);
                    // Chat keys act on release, as TextBox's KEY_UP listeners.
                    injected.type = SDL_KEYUP; injected.key.state = SDL_RELEASED; SDL_PushEvent(&injected);
                } else if (step.action == "hold" || step.action == "release") {
                    // Polled controls read SDL's keyboard state, which pushed events cannot change.
                    const SDL_Keycode key = SDL_GetKeyFromName(step.argument.c_str());
                    if (key == SDLK_UNKNOWN) throw std::invalid_argument("Unknown script key: " + step.argument);
                    if (step.action == "hold") script_keys.insert(SDL_GetScancodeFromKey(key)); else script_keys.erase(SDL_GetScancodeFromKey(key));
                } else if (step.action == "text") {
                    // sdl2-compat crashes converting pushed SDL_TEXTINPUT events.
                    if(dialog.visible()||members.visible()||(skins&&skins->visible())) {} else if(board.visible())board.insert(step.argument);else if(interaction.visible())interaction.text(step.argument);else if(chat.typing())chat.text(step.argument);else if (startup) startup->text(step.argument);
                } else if(step.action=="resize") {
                    if(step.x<1||step.y<1)throw std::invalid_argument("Invalid scripted window size");
                    SDL_SetWindowSize(session.window,step.x,step.y);
                } else if (step.action == "capture") script_capture = step.argument;
                else running = false;
            }
            if(dialog.visible()) {
                int dw{},dh{};SDL_GL_GetDrawableSize(session.window,&dw,&dh);
                if(dh>0) { fonts.set_pixel_scale(dh/600.0);if(dialog.update(fonts,dw,dh))dialog_texture.update(dialog.image()); }
            }
            if(members.visible()) {
                int dw{},dh{};SDL_GL_GetDrawableSize(session.window,&dw,&dh);
                if(dh>0) { fonts.set_pixel_scale(dh/600.0);if(members.update(fonts,dw,dh,dt))members_texture.update(members.image()); }
            }
            if(skins&&skins->visible()) { int dw{},dh{};SDL_GL_GetDrawableSize(session.window,&dw,&dh);if(dh>0) { fonts.set_pixel_scale(dh/600.0);if(skins->update(dw,dh,time))skin_texture.update(skins->image()); } }
            if(options_screen.visible()) { int dw{},dh{};SDL_GL_GetDrawableSize(session.window,&dw,&dh);if(dh>0) { fonts.set_pixel_scale(dh/600.0);if(options_screen.update(fonts,dw,dh,time))options_texture.update(options_screen.image()); } }
            if(board.visible()) {
                int dw{},dh{};SDL_GL_GetDrawableSize(session.window,&dw,&dh);
                if(dh>0) { fonts.set_pixel_scale(dh/600.0);if(board.update(fonts,dw,dh,dt))board_texture.update(board.image()); }
            }
            { REALM_ZONE("events.poll_dispatch");
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if(telemetry&&(event.type==SDL_KEYDOWN||event.type==SDL_KEYUP||event.type==SDL_MOUSEMOTION||event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP||event.type==SDL_TEXTINPUT||event.type==SDL_MOUSEWHEEL)) {
                    input_observed=true;const auto age=static_cast<std::uint32_t>(SDL_GetTicks()-event.common.timestamp);
                    // SDL timestamps use its monotonic millisecond tick clock. Synthetic
                    // zero/invalid timestamps are observation-to-submit samples.
                    input_age=std::max(input_age,(event.common.timestamp&&age<60000 ? static_cast<double>(age) : 0)-milliseconds(Clock::now()-start));
                }
                if (event.type == SDL_QUIT) running = false;
                if(event.type==SDL_MOUSEBUTTONUP&&event.button.button==SDL_BUTTON_LEFT)skin_press_guard=false;
                if (menus) {
                    int lw{}, lh{}, dw{}, dh{}; SDL_GetWindowSize(session.window, &lw, &lh); SDL_GL_GetDrawableSize(session.window, &dw, &dh);
                    const auto pixels = [&](int x, int y) { return realm::Vec2{lw > 0 ? static_cast<double>(x) * dw / lw : x, lh > 0 ? static_cast<double>(y) * dh / lh : y}; };
                    switch (event.type) {
                    case SDL_WINDOWEVENT:
                        if(event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED&&dh>0) {
                            fonts.set_pixel_scale(dh/600.0);startup->update(time,0,dw,dh);
                        }
                        break;
                    case SDL_MOUSEMOTION: startup->pointer_move(pixels(event.motion.x, event.motion.y)); break;
                    case SDL_MOUSEBUTTONDOWN: if (event.button.button == SDL_BUTTON_LEFT) startup->pointer_down(pixels(event.button.x, event.button.y)); break;
                    case SDL_MOUSEBUTTONUP: if (event.button.button == SDL_BUTTON_LEFT) startup->pointer_up(pixels(event.button.x, event.button.y)); break;
                    case SDL_MOUSEWHEEL: startup->wheel(event.wheel.y); break;
                    case SDL_TEXTINPUT: startup->text(event.text.text); break;
                    case SDL_KEYDOWN: {
                        const auto key = event.key.keysym.sym;
                        std::optional<realm::StartupKey> menu_key;
                        if (key == SDLK_BACKSPACE) menu_key = realm::StartupKey::backspace;
                        else if (key == SDLK_TAB) menu_key = realm::StartupKey::tab;
                        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) menu_key = realm::StartupKey::enter;
                        else if (key == SDLK_ESCAPE) menu_key = realm::StartupKey::escape;
                        if (menu_key && !startup->key(*menu_key) && key == SDLK_ESCAPE && !event.key.repeat) running = false;
                        break;
                    }
                    default: break;
                    }
                    continue;
                }
                if(dialog.visible()) {
                    int lw{},lh{},dw{},dh{};SDL_GetWindowSize(session.window,&lw,&lh);SDL_GL_GetDrawableSize(session.window,&dw,&dh);
                    const auto point=[&](int x,int y) { return realm::Vec2{lw>0 ? static_cast<double>(x)*dw/lw : x,lh>0 ? static_cast<double>(y)*dh/lh : y}; };
                    switch(event.type) {
                    case SDL_MOUSEMOTION:dialog.pointer_move(point(event.motion.x,event.motion.y));break;
                    case SDL_MOUSEBUTTONDOWN:if(event.button.button==SDL_BUTTON_LEFT)dialog.pointer_down(point(event.button.x,event.button.y));break;
                    case SDL_MOUSEBUTTONUP:if(event.button.button==SDL_BUTTON_LEFT)(void)dialog.pointer_up(point(event.button.x,event.button.y));break;
                    case SDL_WINDOWEVENT:
                        if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST||event.window.event==SDL_WINDOWEVENT_LEAVE)dialog.focus_lost();
                        if(event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED&&dh>0) { fonts.set_pixel_scale(dh/600.0);if(dialog.update(fonts,dw,dh))dialog_texture.update(dialog.image()); }
                        break;
                    default:break;
                    }
                    // Dialog has click events only. Keyboard, wheel and all mouse
                    // buttons are consumed by the modal layer before game controls.
                    continue;
                }
                if(options_screen.visible()) {
                    int lw{},lh{},dw{},dh{};SDL_GetWindowSize(session.window,&lw,&lh);SDL_GL_GetDrawableSize(session.window,&dw,&dh);
                    const auto point=[&](int x,int y) { return realm::Vec2{static_cast<double>(x)*dw/std::max(1,lw),static_cast<double>(y)*dh/std::max(1,lh)}; };
                    switch(event.type) {
                    case SDL_MOUSEMOTION:options_screen.pointer_move(point(event.motion.x,event.motion.y));break;
                    case SDL_MOUSEBUTTONDOWN:if(event.button.button==SDL_BUTTON_LEFT)options_screen.pointer_down(point(event.button.x,event.button.y));break;
                    case SDL_MOUSEBUTTONUP:if(event.button.button==SDL_BUTTON_LEFT)options_screen.pointer_up(point(event.button.x,event.button.y));break;
                    // Auto-repeat would act on the key that has just been bound.
                    case SDL_KEYDOWN:if(!event.key.repeat)options_screen.key_down(realm::sdl_keys::flash_code(event.key.keysym.sym));break;
                    case SDL_WINDOWEVENT:
                        if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST||event.window.event==SDL_WINDOWEVENT_LEAVE)options_screen.focus_lost();
                        if(event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED&&dh>0) { fonts.set_pixel_scale(dh/600.0);if(options_screen.update(fonts,dw,dh,time))options_texture.update(options_screen.image()); }
                        break;
                    default:break;
                    }
                    if(options_screen.take_changed()) { apply_settings();save_settings(); }
                    if(options_screen.take_home()&&startup&&game_session&&!leaving_game) { leaving_game=true;game_session->disconnect(); }
                    hud_input.cancel();hud_mouse_down=false;tooltip_pointer_valid=false;continue;
                }
                if(skins&&skins->visible()) {
                    int lw{},lh{},dw{},dh{};SDL_GetWindowSize(session.window,&lw,&lh);SDL_GL_GetDrawableSize(session.window,&dw,&dh);
                    const auto point=[&](int x,int y) { return realm::Vec2{static_cast<double>(x)*dw/std::max(1,lw),static_cast<double>(y)*dh/std::max(1,lh)}; };
                    switch(event.type) {
                    case SDL_MOUSEMOTION:skins->pointer_move(point(event.motion.x,event.motion.y));break;
                    case SDL_MOUSEBUTTONDOWN:if(event.button.button==SDL_BUTTON_LEFT) { skin_press_guard=true;skins->pointer_down(point(event.button.x,event.button.y)); }break;
                    case SDL_MOUSEBUTTONUP:if(event.button.button==SDL_BUTTON_LEFT)skins->pointer_up(point(event.button.x,event.button.y));break;
                    case SDL_MOUSEWHEEL:skins->wheel(mouse_wheel_steps(event.wheel));break;
                    case SDL_WINDOWEVENT:
                        if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST||event.window.event==SDL_WINDOWEVENT_LEAVE)skins->focus_lost();
                        if(event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED&&dh>0) { fonts.set_pixel_scale(dh/600.0);if(skins->update(dw,dh,time))skin_texture.update(skins->image()); }break;
                    default:break;
                    }
                    hud_input.cancel();hud_mouse_down=false;tooltip_pointer_valid=false;continue;
                }
                if(members.visible()) {
                    int lw{},lh{},dw{},dh{};SDL_GetWindowSize(session.window,&lw,&lh);SDL_GL_GetDrawableSize(session.window,&dw,&dh);
                    const auto point=[&](int x,int y) { return realm::Vec2{lw>0 ? static_cast<double>(x)*dw/lw : x,lh>0 ? static_cast<double>(y)*dh/lh : y}; };
                    switch(event.type) {
                    case SDL_MOUSEMOTION:members.pointer_move(point(event.motion.x,event.motion.y));break;
                    case SDL_MOUSEBUTTONDOWN:if(event.button.button==SDL_BUTTON_LEFT)members.pointer_down(point(event.button.x,event.button.y));break;
                    case SDL_MOUSEBUTTONUP:if(event.button.button==SDL_BUTTON_LEFT)members.pointer_up(point(event.button.x,event.button.y));break;
                    case SDL_MOUSEWHEEL:members.wheel(mouse_wheel_steps(event.wheel));break;
                    case SDL_WINDOWEVENT:
                        if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST||event.window.event==SDL_WINDOWEVENT_LEAVE)members.focus_lost();
                        if(event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED&&dh>0) { fonts.set_pixel_scale(dh/600.0);if(members.update(fonts,dw,dh))members_texture.update(members.image()); }
                        break;
                    default:break;
                    }
                    hud_input.cancel();hud_mouse_down=false;tooltip_pointer_valid=false;continue;
                }
                if(board.visible()) {
                    int lw{},lh{},dw{},dh{};SDL_GetWindowSize(session.window,&lw,&lh);SDL_GL_GetDrawableSize(session.window,&dw,&dh);
                    const auto point=[&](int x,int y) { return realm::Vec2{lw>0 ? static_cast<double>(x)*dw/lw : x,lh>0 ? static_cast<double>(y)*dh/lh : y}; };
                    switch(event.type) {
                    case SDL_MOUSEMOTION:board.pointer_move(point(event.motion.x,event.motion.y));break;
                    case SDL_MOUSEBUTTONDOWN:if(event.button.button==SDL_BUTTON_LEFT)board.pointer_down(point(event.button.x,event.button.y),(SDL_GetModState()&KMOD_SHIFT)!=0);break;
                    case SDL_MOUSEBUTTONUP:if(event.button.button==SDL_BUTTON_LEFT)board.pointer_up(point(event.button.x,event.button.y));break;
                    case SDL_MOUSEWHEEL:board.wheel(event.wheel.y);break;
                    case SDL_TEXTINPUT:board.insert(event.text.text);break;
                    case SDL_KEYDOWN: {
                        const auto k=event.key.keysym.sym;const bool shift=(event.key.keysym.mod&KMOD_SHIFT)!=0,ctrl=(event.key.keysym.mod&KMOD_CTRL)!=0;
                        using K=realm::GuildBoard::Key;std::optional<K> key;
                        if(k==SDLK_LEFT)key=K::left;else if(k==SDLK_RIGHT)key=K::right;else if(k==SDLK_UP)key=K::up;else if(k==SDLK_DOWN)key=K::down;
                        else if(k==SDLK_HOME)key=K::home;else if(k==SDLK_END)key=K::end;else if(k==SDLK_BACKSPACE)key=K::backspace;else if(k==SDLK_DELETE)key=K::del;
                        else if(k==SDLK_RETURN||k==SDLK_KP_ENTER)key=K::enter;else if(ctrl&&k==SDLK_a)key=K::select_all;
                        if(key)board.key(*key,shift,ctrl);
                        if(ctrl&&board.wants_text()) {
                            if(k==SDLK_c||k==SDLK_x) { const auto selected_text=board.selection();if(!selected_text.empty())SDL_SetClipboardText(selected_text.c_str());if(k==SDLK_x)board.cut(); }
                            if(k==SDLK_v) { char* pasted=SDL_GetClipboardText();if(pasted) { board.insert(pasted);SDL_free(pasted); } }
                        }
                        break;
                    }
                    case SDL_WINDOWEVENT:
                        if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST||event.window.event==SDL_WINDOWEVENT_LEAVE)board.focus_lost();
                        if(event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED&&dh>0) { fonts.set_pixel_scale(dh/600.0);if(board.update(fonts,dw,dh))board_texture.update(board.image()); }
                        break;
                    default:break;
                    }
                    if(auto link=board.take_link())SDL_OpenURL(link->c_str());
                    continue;
                }
                if(interaction.visible()) {
                    int lw{},lh{},dw{},dh{};SDL_GetWindowSize(session.window,&lw,&lh);SDL_GL_GetDrawableSize(session.window,&dw,&dh);
                    const auto point=[&](int x,int y) { return realm::InteractionDialog::local({lw>0 ? static_cast<double>(x)*dw/lw : x,lh>0 ? static_cast<double>(y)*dh/lh : y},dw,dh); };
                    switch(event.type) {
                    case SDL_MOUSEBUTTONDOWN:if(event.button.button==SDL_BUTTON_LEFT)interaction.pointer_down(point(event.button.x,event.button.y));break;
                    case SDL_MOUSEBUTTONUP:if(event.button.button==SDL_BUTTON_LEFT)interaction.pointer_up(point(event.button.x,event.button.y));break;
                    case SDL_TEXTINPUT:interaction.text(event.text.text);break;
                    case SDL_KEYDOWN:
                        if(event.key.keysym.sym==SDLK_ESCAPE)interaction.cancel();
                        else if(event.key.keysym.sym==SDLK_BACKSPACE)interaction.backspace();
                        else if(!event.key.repeat&&(event.key.keysym.sym==SDLK_RETURN||event.key.keysym.sym==SDLK_KP_ENTER))interaction.submit();
                        break;
                    case SDL_WINDOWEVENT:if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST)interaction.focus_lost();break;
                    default:break;
                    }
                    continue;
                }
                // TextBox: chat keys act on release; an open field takes all keyboard input. The keys that
                // open it, start a command or tell, and scroll are the Chat tab's bindings.
                if(chat_view&&o.world&&game_session&&game_session->state()==realm::SessionState::playing&&(event.type==SDL_KEYDOWN||event.type==SDL_KEYUP||event.type==SDL_TEXTINPUT)) {
                    const auto key=event.type==SDL_TEXTINPUT ? SDLK_UNKNOWN : event.key.keysym.sym;
                    const int code=realm::sdl_keys::flash_code(key);
                    const auto bound=[&](std::string_view param) { return code!=0&&code==settings.get(param); };
                    const bool shift=event.type!=SDL_TEXTINPUT&&(event.key.keysym.mod&KMOD_SHIFT)!=0;
                    if(event.type==SDL_KEYUP&&bound("scrollChatUp"))chat.scroll_up();
                    if(event.type==SDL_KEYUP&&bound("scrollChatDown"))chat.scroll_down();
                    if(chat.typing()) {
                        if(event.type==SDL_TEXTINPUT)chat.text(event.text.text);
                        else if(event.type==SDL_KEYDOWN&&key==SDLK_BACKSPACE)chat.backspace();
                        // Flash has no Escape binding here; closing the field keeps Escape from leaving the game.
                        else if(event.type==SDL_KEYDOWN&&key==SDLK_ESCAPE)chat.close();
                        else if(event.type==SDL_KEYUP&&(key==SDLK_RETURN||key==SDLK_KP_ENTER)) { if(const auto line=chat.submit())game_session->player_text(*line); }
                        else if(event.type==SDL_KEYUP&&bound("tell"))chat.tell(shift);
                        continue;
                    }
                    if(event.type==SDL_KEYUP&&!interaction.visible()) {
                        if(bound("chat"))chat.open();
                        else if(bound("chatCommand"))chat.open("/");
                        else if(bound("tell"))chat.tell(shift);
                        else if(bound("guildChat"))chat.open("/g ");
                    }
                }
                if(event.type==SDL_WINDOWEVENT&&(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST||event.window.event==SDL_WINDOWEVENT_LEAVE)) {
                    tooltip_pointer_valid=false;
                    if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) { hud_input.cancel();hud_mouse_down=false; }
                }
                if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED) {
                    // A previously hovered field may have moved beneath a stationary cursor.
                    tooltip_pointer_valid=false;
                    int mx{},my{},lw{},lh{};SDL_GetMouseState(&mx,&my);SDL_GetWindowSize(session.window,&lw,&lh);
                    if(lh>0)hud_input.move({(mx-lw)/(lh/600.0)+200,my/(lh/600.0)},hud_input.selected(),hud_container>=0);
                }
                if(event.type==SDL_MOUSEMOTION)tooltip_pointer_valid=true;
                if(event.type==SDL_MOUSEBUTTONDOWN&&event.button.button==SDL_BUTTON_LEFT)hud_mouse_down=true;
                if(event.type==SDL_MOUSEBUTTONUP&&event.button.button==SDL_BUTTON_LEFT)hud_mouse_down=false;
                if(event.type==SDL_MOUSEWHEEL&&tooltip&&tooltip->visible()) {
                    const int steps=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
                    if(tooltip->scroll(steps))tooltip_texture.update(tooltip->image());
                }
                // PlayerMenu sits on the stage above the HUD and receives its clicks first.
                const auto window_point=[&](int x,int y) {
                    int lw{},lh{},dw{},dh{};SDL_GetWindowSize(session.window,&lw,&lh);SDL_GL_GetDrawableSize(session.window,&dw,&dh);
                    return realm::Vec2{lw>0 ? static_cast<double>(x)*dw/lw : x,lh>0 ? static_cast<double>(y)*dh/lh : y};
                };
                if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
                    if(player_group) { player_group->focus_lost(); } group_pressed=minimap_pressed=arrow_pressed=false;party_arrows.reset();
                }
                bool menu_consumed{};
                if(event.type==SDL_MOUSEBUTTONUP&&event.button.button==SDL_BUTTON_LEFT&&std::exchange(arrow_pressed,false))menu_consumed=true;
                if(!menu_consumed&&player_group&&game_session&&(event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP)&&event.button.button==SDL_BUTTON_LEFT) {
                    const auto at=window_point(event.button.x,event.button.y);
                    if(event.type==SDL_MOUSEBUTTONDOWN) {
                        if(player_group->menu()&&player_group->pointer_down(at))menu_consumed=group_pressed=true;
                    } else if(std::exchange(group_pressed,false)) {
                        menu_consumed=true;
                        if(player_group->pointer_up(at)) {
                            if(const auto id=realm::first_teleport_player(world,player_group->players())) {
                                if(const auto refusal=game_session->teleport(*id,time))chat.add(std::string(realm::error_chat_name),*refusal,time);
                            } else chat.add(std::string(realm::error_chat_name),std::string(realm::no_teleport_players),time);
                            player_group->close();
                        }
                    }
                }
                if(!menu_consumed&&event.type==SDL_MOUSEBUTTONDOWN&&event.button.button==SDL_BUTTON_LEFT&&player_group&&game_session) {
                    const auto at=window_point(event.button.x,event.button.y);
                    if(!(player_menu&&player_menu->contains(at)))if(const auto* arrow=party_arrows.hit(at)) {
                        auto players=realm::player_group_targets(world,arrow->players);
                        if(player_menu)player_menu->close();
                        int dw{},dh{};SDL_GL_GetDrawableSize(session.window,&dw,&dh);
                        if(player_group->open(std::move(players),game_session->map_info().allow_teleport,at,dw,dh,dh/600.0))group_texture.update(player_group->image());
                        menu_consumed=arrow_pressed=true;
                    }
                }
                if(!menu_consumed&&player_menu&&(event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP)&&event.button.button==SDL_BUTTON_LEFT) {
                    const auto at=window_point(event.button.x,event.button.y);
                    if(event.type==SDL_MOUSEBUTTONDOWN) { if(player_menu->visible()&&player_menu->pointer_down(at))menu_consumed=player_menu_pressed=true; }
                    else if(std::exchange(player_menu_pressed,false)) {
                        menu_consumed=true;
                        if(const auto request=player_menu->pointer_up(at);request&&game_session) {
                            using A=realm::PlayerMenuAction;
                            switch(request->action) {
                            case A::teleport: if(const auto refusal=game_session->teleport(request->object_id,time))chat.add(std::string(realm::error_chat_name),*refusal,time);break;
                            case A::invite: game_session->guild_invite(request->name);break;
                            case A::trade: game_session->request_trade(request->name);break;
                            case A::lock: game_session->edit_account_list(request->object_id,false,true);break;
                            case A::unlock: game_session->edit_account_list(request->object_id,false,false);break;
                            case A::ignore: game_session->edit_account_list(request->object_id,true,true);break;
                            case A::unignore: game_session->edit_account_list(request->object_id,true,false);break;
                            }
                        }
                    }
                }
                // TradePanel covers the HUD below the minimap; TradeRequestPanel covers the interaction panel.
                if(trade_view&&!menu_consumed&&game_session&&(event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP)&&event.button.button==SDL_BUTTON_LEFT&&(trade.active()||trade.request())) {
                    int lw{},lh{};SDL_GetWindowSize(session.window,&lw,&lh);
                    const realm::Vec2 point{lh>0 ? (event.button.x-lw)/(lh/600.0)+200 : -1,lh>0 ? event.button.y/(lh/600.0) : -1};
                    const auto hit=[&](realm::HudRect area) { return point.x>=area.x&&point.x<area.x+area.width&&point.y>=area.y&&point.y<area.y+area.height; };
                    const bool down=event.type==SDL_MOUSEBUTTONDOWN;
                    if(trade.active()&&point.x>=0&&point.y>=realm::TradeView::panel_top) {
                        menu_consumed=true;
                        if(down) {
                            trade_pressed=hit(realm::TradeView::cancel_button) ? 0 : hit(realm::TradeView::trade_button) ? 1 : -1;
                            // TradeInventory toggles offers on MOUSE_DOWN.
                            if(const int slot=realm::TradeView::slot_at(point);slot>=0)if(const auto offer=trade.toggle(static_cast<std::size_t>(slot),time))game_session->change_trade(*offer);
                        } else {
                            const int pressed=std::exchange(trade_pressed,-1);
                            if(pressed==0&&hit(realm::TradeView::cancel_button)) { game_session->cancel_trade();trade.end(); }
                            if(pressed==1&&hit(realm::TradeView::trade_button))if(const auto offers=trade.accept(time))game_session->accept_trade(offers->first,offers->second);
                        }
                    } else if(!trade.active()&&trade.request()&&point.x>=0&&point.y>=realm::TradeView::request_top) {
                        menu_consumed=true;const auto buttons=trade_view->request_buttons();
                        if(down)trade_pressed=hit(buttons.reject) ? 2 : hit(buttons.accept) ? 3 : -1;
                        else {
                            const int pressed=std::exchange(trade_pressed,-1);
                            if(pressed==2&&hit(buttons.reject))trade.close_request();
                            // Accepting sends the reverse request that starts the trade.
                            if(pressed==3&&hit(buttons.accept)) { game_session->request_trade(*trade.request());trade.close_request(); }
                        }
                    }
                }
                if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED)trade.activate(time);
                if(!menu_consumed&&hud_art&&hud&&o.world&&(event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP||event.type==SDL_MOUSEMOTION))if(const auto* player=world.entity(world.player_id())) {
                    int lw{},lh{};SDL_GetWindowSize(session.window,&lw,&lh);
                    if(lh>0) {
                        const int mx=event.type==SDL_MOUSEMOTION?event.motion.x:event.button.x,my=event.type==SDL_MOUSEMOTION?event.motion.y:event.button.y;
                        const realm::Vec2 point{(mx-lw)/(lh/600.0)+200,my/(lh/600.0)};
                        auto state=realm::HudState::player(world,*player).inventory;
                        if(state.container!=hud_container) { hud_input.container_changed();hud_container=state.container; }
                        if(event.type==SDL_MOUSEMOTION)hud_input.move(point,hud_input.selected(),state.container>=0);
                        else if(event.button.button==SDL_BUTTON_LEFT) {
                            const auto& panel=hud_view;const auto button=panel.panel_button(fonts);
                            // PartyPanel.onMouseDown replaces any open menu with one for the member clicked.
                            if(event.type==SDL_MOUSEBUTTONDOWN&&player_menu&&game_session&&panel.party.shown)if(const int cell=realm::PartyHover::cell(point);cell>=0) {
                                std::int32_t id=party_hover.target();
                                if(id<0&&panel.party.members.at(static_cast<std::size_t>(cell)))id=panel.party.members.at(static_cast<std::size_t>(cell))->object_id;
                                player_menu->close();if(player_group)player_group->close();
                                if(const auto target=realm::player_menu_target(world,game_session->party(),id,game_session->map_info().allow_teleport)) {
                                    int dw{},dh{};SDL_GL_GetDrawableSize(session.window,&dw,&dh);
                                    if(dh>0&&player_menu->open(*target,window_point(event.button.x,event.button.y),dw,dh,dh/600.0))player_menu_texture.update(player_menu->image());
                                }
                            }
                            // CharacterDetailsView's IconButton: Options in the Nexus, otherwise escape to the Nexus.
                            const bool header_over=panel.header.button!=realm::HudState::Header::Button::none&&!trade.active()&&realm::HudInput::button_at(realm::header_button,point);
                            if(event.type==SDL_MOUSEBUTTONDOWN)header_pressed=header_over;
                            else if(std::exchange(header_pressed,false)&&header_over) {
                                if(panel.header.button==realm::HudState::Header::Button::options)open_options();
                                else if(game_session)game_session->escape();
                            }
                            if(event.type==SDL_MOUSEBUTTONDOWN)hud_input.button_down(point,button);
                            else if(hud_input.button_up(point,button)&&game_session) {
                                if(panel.portal.id==button.id)game_session->use_portal(button.id);
                                else if(panel.social.id==button.id) {
                                    if(panel.social.kind==realm::HudState::SocialKind::hall)game_session->use_portal(button.id);
                                    else if(interaction.open(panel.social,*player)) { hud_input.cancel();hud_mouse_down=false;tooltip_pointer_valid=false; }
                                }
                                else if(panel.sellable.id==button.id)game_session->buy(button.id);
                                // GuildBoardPanel opens with the current authoritative rank.
                                else if(panel.button.kind==0) {
                                    board.open(app_host,o.app_port,game_session->credentials(),(player->stats.contains(static_cast<std::uint8_t>(realm::flash::StatId::GUILD_RANK_STAT)) ? player->stats.at(static_cast<std::uint8_t>(realm::flash::StatId::GUILD_RANK_STAT)) : 0));
                                    hud_input.cancel();hud_mouse_down=false;tooltip_pointer_valid=false;chat.close();party_hover.reset();if(player_menu) { player_menu->close(); } if(player_group)player_group->close();group_pressed=minimap_pressed=arrow_pressed=false;party_arrows.reset();
                                }
                                else if(panel.button.kind==1) {
                                    members.open(app_host,o.app_port,game_session->credentials(),player->guild,player->name,(player->stats.contains(static_cast<std::uint8_t>(realm::flash::StatId::GUILD_RANK_STAT)) ? player->stats.at(static_cast<std::uint8_t>(realm::flash::StatId::GUILD_RANK_STAT)) : 0));
                                    hud_input.cancel();hud_mouse_down=false;tooltip_pointer_valid=false;chat.close();party_hover.reset();if(player_menu) { player_menu->close(); } if(player_group)player_group->close();group_pressed=minimap_pressed=arrow_pressed=false;party_arrows.reset();
                                }
                                // CharacterChangerPanel dispatches GameSprite.closed.
                                else if(panel.button.kind==3&&skins) {
                                    skins->endpoint(app_host,o.app_port,game_session->credentials());
                                    if(startup&&startup->char_list())skins->open(player->type,player->texture,*startup->char_list());
                                    else skins->fetch(player->type,player->texture,app_host,o.app_port,game_session->credentials());
                                    hud_input.cancel();hud_mouse_down=false;tooltip_pointer_valid=false;chat.close();party_hover.reset();if(player_menu)player_menu->close();if(player_group)player_group->close();party_arrows.reset();
                                }
                                else if(panel.button.kind==2&&startup) { leaving_game=true;game_session->disconnect(); }
                            }
                            if(event.type==SDL_MOUSEBUTTONDOWN)hud_input.down(point,state);
                            else if(const auto action=hud_input.up(point,(SDL_GetModState()&KMOD_SHIFT)!=0,(SDL_GetModState()&KMOD_CTRL)!=0,time,state);action&&game_session) {
                                const auto slot=[&](int index) { return index>=realm::container_slot ? realm::net::Slot{state.container,static_cast<std::uint8_t>(index-realm::container_slot)} : realm::net::Slot{player->id,static_cast<std::uint8_t>(index)}; };
                                const auto source=slot(action->source);
                                switch(action->kind) {
                                case realm::InventoryActionKind::swap: game_session->swap_items(source,slot(action->target),time);break;
                                // ItemGridMediator.dropItem sends nothing for container tiles.
                                case realm::InventoryActionKind::drop:if(action->source<realm::container_slot)game_session->drop_to_ground(action->source,time);break;
                                case realm::InventoryActionKind::use:game_session->use_item(source,time);break;
                                case realm::InventoryActionKind::equip:game_session->equip_or_use(source,time);break;
                                case realm::InventoryActionKind::potion:game_session->use_potion(action->source,time);break;
                                case realm::InventoryActionKind::transfer:
                                case realm::InventoryActionKind::store: {
                                    const bool transfer=action->kind==realm::InventoryActionKind::transfer;
                                    if(transfer&&!state.backpack)break;
                                    const int begin=transfer&&hud_input.selected()!=2?12:4,end=transfer?begin+8:(state.backpack?20:12);
                                    for(int i=begin;i<end;++i)if(i!=action->source&&state.items.at(static_cast<std::size_t>(i))<=0) { game_session->swap_items(source,{player->id,static_cast<std::uint8_t>(i)},time);break; }
                                    break;
                                }
                                }
                            }
                        }
                    }
                }
                if(!menu_consumed&&(event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP)&&event.button.button==SDL_BUTTON_LEFT&&o.world&&minimap) {
                    int lw{},lh{},dw{},dh{};SDL_GetWindowSize(session.window,&lw,&lh);SDL_GL_GetDrawableSize(session.window,&dw,&dh);
                    if(lh>0) {
                        const double scale=lh/600.0,x=(event.button.x-lw)/scale+196,y=event.button.y/scale-4;
                        const bool inside=x>=0&&x<192&&y>=0&&y<192;
                        const bool zoom_button=x>=172&&x<188&&y>=4&&y<30;
                        if(event.type==SDL_MOUSEBUTTONDOWN) {
                            minimap_pressed=inside;
                            if(inside&&zoom_button) { if(y>=14)minimap->zoom_out();else minimap->zoom_in(); }
                        } else if(std::exchange(minimap_pressed,false)&&inside&&!zoom_button&&player_group&&player_group->visible()&&!player_group->menu()&&game_session) {
                            auto players=player_group->players();
                            if(player_menu)player_menu->close();
                            if(player_group->open(std::move(players),game_session->map_info().allow_teleport,window_point(event.button.x,event.button.y),dw,dh,dh/600.0))group_texture.update(player_group->image());
                        }
                    }
                }
                if (event.type == SDL_KEYDOWN) {
                    const auto key = event.key.keysym.sym;
                    // MapUserInput.onKeyDown: every action is the key its Options row binds.
                    const int code = realm::sdl_keys::flash_code(key);
                    const auto bound = [&](std::string_view param) { return code != 0 && code == settings.get(param); };
                    const bool fresh = !event.key.repeat;
                    if (key == SDLK_ESCAPE) {
                        if (startup && game_session) { if (!event.key.repeat) { leaving_game = true; game_session->disconnect(); } }
                        else running = false;
                    }
                    if (game_session && fresh && (bound("escapeToNexus") || bound("escapeToNexus2"))) game_session->escape();
                    if (fresh && bound("resetToDefaultCameraAngle")) { angle = settings.default_camera_angle(); save_settings(); }
                    if (bound("options") && fresh && hud && o.world) open_options();
                    if (key == SDLK_TAB && !startup) { o.world = !o.world;hud_input.cancel(); }
                    if (bound("switchTabs") && hud_art && hud && o.world && fresh)if(const auto* player=world.entity(world.player_id()))hud_input.cycle_tabs(realm::HudState::player(world,*player).inventory.backpack);
                    static constexpr std::array<std::string_view, 8> inventory_keys{"useEquipInvSlot1", "useEquipInvSlot2", "useEquipInvSlot3", "useEquipInvSlot4", "useEquipInvSlot5", "useEquipInvSlot6", "useEquipInvSlot7", "useEquipInvSlot8"};
                    if (hud_art && o.world && fresh && game_session)
                        for (std::size_t slot = 0; slot < inventory_keys.size(); ++slot)
                            if (bound(inventory_keys[slot]))if(const auto* player=world.entity(world.player_id()))game_session->use_item({player->id,static_cast<std::uint8_t>((hud_input.selected()==2?12:4)+slot)},time);
                    // MapUserInput handles every KEY_DOWN, including auto-repeat.
                    if(game_session&&bound("useHealthPotion"))game_session->use_potion(false,time);
                    if(game_session&&bound("useMagicPotion"))game_session->use_potion(true,time);
                    // PortalPanel.onKeyDown: the interact key enters even while Full or Locked.
                    if(game_session&&hud_art&&o.world&&bound("interact"))if(const auto* player=world.entity(world.player_id())) {
                        const auto state=realm::HudState::player(world,*player);
                        if(state.portal.id>=0)game_session->use_portal(state.portal.id);
                        else if(state.social.kind==realm::HudState::SocialKind::hall&&state.social.enabled)game_session->use_portal(state.social.id);
                    }
                    if (fresh && bound("autofireToggle")) autofire = !autofire;
                    if (fresh && bound("toggleCentering")) { settings.set_choice("centerOnPlayer", settings.flag("centerOnPlayer") ? 0 : 1); apply_settings(); save_settings(); }
                    if (fresh && bound("togglePerformanceStats")) { perf_stats = !perf_stats; perf_dirty = true; }
                    if (fresh && bound("toggleFullscreenMode") && !o.hidden) {
                        const bool fullscreen = (SDL_GetWindowFlags(session.window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
                        SDL_SetWindowFullscreen(session.window, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                    // Harness fixtures: the camera fixture's own keys.
                    if (key == SDLK_c && !startup) centered = !centered;
                    if (key == SDLK_RIGHT) { selected = (selected + 1) % realm::flash::sheets.size(); page = 0; }
                    if (key == SDLK_LEFT) { selected = (selected + realm::flash::sheets.size() - 1) % realm::flash::sheets.size(); page = 0; }
                    if (key == SDLK_DOWN) ++page;
                    if (key == SDLK_UP) page = std::max(0, page - 1);
                    if (o.world && minimap) {
                        if (bound("miniMapZoomIn")) minimap->zoom_in();
                        if (bound("miniMapZoomOut")) minimap->zoom_out();
                    } else {
                        if (key == SDLK_EQUALS || key == SDLK_KP_PLUS) zoom = std::min(4.0, zoom * 1.25);
                        if (key == SDLK_MINUS || key == SDLK_KP_MINUS) zoom = std::max(.25, zoom / 1.25);
                    }
                }
            }
            }
            if(auto request=interaction.take_request();request&&game_session&&game_session->state()==realm::SessionState::playing) {
                using K=realm::HudState::SocialKind;
                if(request->kind==K::create_guild)game_session->create_guild(request->value);
                else if(request->kind==K::leave_guild)game_session->leave_guild(request->value);
                else game_session->choose_name(request->value);
            }
            if(skins)if(auto selected_skin=skins->take_selection();selected_skin&&*selected_skin>=0&&game_session) { (void)game_session->reskin(*selected_skin,skins->data().owned_skins); }
            const auto keys = SDL_GetKeyboardState(nullptr);
            // A control is down while the key its Options row binds is held (or a script holds it).
            const auto down = [&](std::string_view param) {
                const int code = settings.get(param);
                if (code == 0) return false;
                if (realm::sdl_keys::held(keys, code)) return true;
                const auto key = realm::sdl_keys::sdl_code(code);
                return key != SDLK_UNKNOWN && script_keys.contains(SDL_GetScancodeFromKey(key));
            };
            if (o.world && !o.benchmark && !menus && !dialog.visible() && !options_screen.visible() && !board.visible() && !members.visible() && !(skins&&skins->visible()) && !interaction.visible()) {
                // Typed characters never move or turn the player. The rotate keys act only with Allow Camera Rotation.
                const double held=chat.typing() ? 0 : 1;
                const bool turning = settings.flag("allowRotation");
                const double rotation = held*(static_cast<double>(turning && down("rotateRight")) - static_cast<double>(turning && down("rotateLeft")));
                const realm::Vec2 input{held*(static_cast<double>(down("moveRight"))-static_cast<double>(down("moveLeft"))),held*(static_cast<double>(down("moveDown"))-static_cast<double>(down("moveUp")))};
                if (auto* player = world.entity(world.player_id())) {
                    realm::move_player(world,*player,input,rotation,dt*1000,angle);position=player->position;
                    if (!game_session) realm::player_move_ack(world,*player);
                } else {
                    angle+=rotation*dt*3;
                    position.x+=(input.x*std::cos(angle)-input.y*std::sin(angle))*dt*5;
                    position.y+=(input.x*std::sin(angle)+input.y*std::cos(angle))*dt*5;
                }
            }
            if (game_session) {
                int mouse_x{},mouse_y{};auto buttons=SDL_GetMouseState(&mouse_x,&mouse_y);
                // SDL_PushEvent does not alter SDL's held-button state. Replay holds
                // explicitly so GPU/network tests exercise the gameplay firing path.
                if(!o.script.empty()) { mouse_x=static_cast<int>(script_pointer.x);mouse_y=static_cast<int>(script_pointer.y);buttons=script_mouse_down ? SDL_BUTTON(SDL_BUTTON_LEFT) : 0; }
                const bool ability_key=(down("useSpecial")||script_ability_down)&&!chat.typing();
                if (!menus && !dialog.visible() && !options_screen.visible() && !board.visible() && !members.visible() && !(skins&&skins->visible()) && !interaction.visible() && ((buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) || autofire || ability_key)) {
                    int logical_w{},logical_h{},drawable_w{},drawable_h{};SDL_GetWindowSize(session.window,&logical_w,&logical_h);SDL_GL_GetDrawableSize(session.window,&drawable_w,&drawable_h);
                    const bool over_menu=skin_press_guard||arrow_pressed||party_arrows.hit({static_cast<double>(mouse_x)*drawable_w/std::max(1,logical_w),static_cast<double>(mouse_y)*drawable_h/std::max(1,logical_h)})||group_pressed||minimap_pressed||(player_group&&logical_w>0&&logical_h>0&&player_group->contains({static_cast<double>(mouse_x)*drawable_w/logical_w,static_cast<double>(mouse_y)*drawable_h/logical_h}))||player_menu_pressed||(player_menu&&logical_w>0&&logical_h>0&&player_menu->contains({static_cast<double>(mouse_x)*drawable_w/logical_w,static_cast<double>(mouse_y)*drawable_h/logical_h}));
                    if(!over_menu&&logical_w>0&&logical_h>0&&drawable_w>drawable_h/3 && mouse_x>=0 && mouse_y>=0 && mouse_y<logical_h && static_cast<double>(mouse_x)*drawable_w/logical_w < drawable_w-drawable_h/3) {
                        camera.configure(position,angle,drawable_w,drawable_h,zoom,centered);
                        const auto target=camera.screen_to_world({static_cast<double>(mouse_x)*drawable_w/logical_w,static_cast<double>(mouse_y)*drawable_h/logical_h});
                        if((buttons & SDL_BUTTON(SDL_BUTTON_LEFT))||autofire) game_session->shoot(std::atan2(target.y-position.y,target.x-position.x),time);
                        if(ability_key) game_session->use_ability(target,time);
                    }
                }
                game_session->finish_frame(time);
            }
            if(!game_session&&!stress)world.particles().update(world,time);
            int width{}, height{}; SDL_GL_GetDrawableSize(session.window, &width, &height);
            if (width <= 0 || height <= 0) { cpu_zone.finish();realm::telemetry::active=nullptr;SDL_Delay(10);previous_capture=true;continue; }
            if(!fonts.empty())fonts.set_pixel_scale(height/600.0);
            if(minimap)minimap->set_pixel_scale(height/600.0);
            if (startup) { REALM_ZONE("ui.startup");startup->update(time, dt*1000, width, height); }
            quads.clear(); int clip_width = width;
            const auto& sheet = realm::flash::sheets[selected];
            if(!o.text.empty()) {
                const auto r=atlas.additional(text_fixture_index);
                auto q=realm::sprite_quad(r,{8+text_fixture.offset.x,8+text_fixture.offset.y},{static_cast<double>(r.width),0},{0,static_cast<double>(r.height)});q.kind=5;quads.push_back(q);
            } else if (!o.texture.empty()) {
                const auto r = atlas.additional(fixture_index);
                if (o.projectile_fixture) quads.push_back(realm::projectile_quad(r,{400,300},o.projectile_angle,o.projectile_size,1,o.projectile_outline));
                else quads.push_back(realm::sprite_quad(r, {8,8}, {static_cast<double>(r.width),0}, {0,static_cast<double>(r.height)}));
            } else if (startup && startup->menus()) {
                if (startup->background() && background_world) {
                    // MapBackground centers an at least 800x600 viewport on the stage.
                    const double view_w = std::max(800, width), view_h = std::max(600, height);
                    const auto at = startup->background_position();
                    camera.configure(at, realm::Startup::background_angle, realm::Rect{-view_w/2, -view_h/2, view_w, view_h});
                    fonts.set_pixel_scale(1);scene.build(world, camera, atlas, animations, o.processed.empty() ? nullptr : &processed, terrain.get(), &appearances, at, realm::Startup::background_angle, 1, width, height, time, quads);fonts.set_pixel_scale(height/600.0);
                }
            } else if (o.world) {
                if (width <= height / 3) throw std::runtime_error("Window too narrow for game camera");
                const auto shake=world.particles().camera_offset(time);
                camera.configure({position.x+shake.x,position.y+shake.y}, angle, width, height, zoom, centered);
                clip_width = static_cast<int>(camera.playable_width());
                if (!o.map.empty() || game_session || stress) {
                    // MAPINFO background (stars / nexus water), redone whenever a new map loads.
                    if (backdrop_generation != world.generation()) {
                        backdrop.reset(game_session ? game_session->map_info().background : o.background, game_session ? game_session->map_info().seed : 1U);
                        backdrop_generation = world.generation();
                    }
                    backdrop.build(camera, atlas, time, quads);
                    fonts.set_pixel_scale(zoom);scene.build(world, camera, atlas, animations, o.processed.empty() ? nullptr : &processed, terrain.get(), &appearances, position, angle, zoom, clip_width, height, o.scene_time>=0 ? o.scene_time : time, quads);fonts.set_pixel_scale(height/600.0);
                } else {
                const int distance = static_cast<int>(std::ceil(camera.max_distance())) + 2;
                if (distance > 120) throw std::runtime_error("Camera fixture exceeds supported visible area");
                const int cx = static_cast<int>(std::floor(position.x)), cy = static_cast<int>(std::floor(position.y));
                for (int y = cy - distance; y <= cy + distance; ++y)
                    for (int x = cx - distance; x <= cx + distance; ++x) {
                        const realm::Vec2 p = camera.world_to_screen({static_cast<double>(x), static_cast<double>(y)});
                        const auto px = camera.world_to_screen({static_cast<double>(x) + 1, static_cast<double>(y)});
                        const auto py = camera.world_to_screen({static_cast<double>(x), static_cast<double>(y) + 1});
                        const double extent = camera.pixels_per_unit() * 1.5;
                        if (p.x < -extent || p.x > clip_width + extent || p.y < -extent || p.y > height + extent) continue;
                        const auto id = static_cast<int>((static_cast<std::uint32_t>(x) * 31U + static_cast<std::uint32_t>(y) * 17U) %
                                                         static_cast<std::uint32_t>(sheet.count));
                        quads.push_back(realm::sprite_quad(atlas.sprite(selected, id), p, {px.x - p.x, px.y - p.y}, {py.x - p.x, py.y - p.y}));
                    }
                // A deterministic ellipse exercises the same visible radial-alpha function.
                const auto p = camera.world_to_screen(position);
                quads.push_back(realm::radial_shadow({p.x - 24 * zoom, p.y - 12 * zoom}, {48 * zoom, 0}, {0, 24 * zoom}, {0, 0, 0}, .5F));
                }
            } else {
                const int cell_w = sheet.width * o.scale + 8, cell_h = sheet.height * o.scale + 8;
                const int cols = std::max(1, (width - 16) / cell_w), rows = std::max(1, (height - 16) / cell_h);
                const int per_page = cols * rows;
                page = std::min(page, std::max(0, (sheet.count - 1) / per_page));
                for (int slot = 0; slot < per_page && page * per_page + slot < sheet.count; ++slot) {
                    quads.push_back(realm::sprite_quad(atlas.sprite(selected, page * per_page + slot),
                        {8.0 + slot % cols * cell_w, 8.0 + slot / cols * cell_h},
                        {static_cast<double>(sheet.width * o.scale), 0}, {0, static_cast<double>(sheet.height * o.scale)}));
                }
            }
            for (int i = 0; i < o.sprites; ++i) {
                const int x = (i * 37) % std::max(1, clip_width - 32), y = (i * 53) % std::max(1, height - 32);
                quads.push_back(realm::sprite_quad(atlas.sprite(selected, i % sheet.count),
                    {static_cast<double>(x), static_cast<double>(y)}, {32, 0}, {0, 32}));
            }
            if (terrain) for (const auto slot : terrain->changes()) atlas.update_additional(terrain_first+slot,terrain->images().at(slot));
            realm::telemetry::Zone ui_zone("ui.build");
            std::array<realm::Quad,128> overlay_storage;std::span<const realm::Quad> overlay;
            if (startup) {
                for (const auto& area : startup->uploads()) startup_texture.update_region(startup->sheet(), area.x, area.y, area.width, area.height);
                startup->clear_uploads();
                menu_quads.clear(); startup->build(startup_texture, time, menu_quads);
                if (!menu_quads.empty()) overlay = menu_quads;
            }
            if(hud&&o.world&&!menus)if(const auto* player=world.entity(world.player_id())) {
                auto state=realm::HudState::player(world,*player,game_session ? &game_session->party() : stress ? stress->party() : nullptr,time);
                const bool hover_enabled=!dialog.visible()&&!board.visible() && !members.visible()&&!(skins&&skins->visible())&&tooltip_pointer_valid&&!hud_mouse_down&&!interaction.visible()&&!trade.active()&&!trade.request()&&game_session&&game_session->state()==realm::SessionState::playing;
                if(game_session)party_hover.update(world,game_session->party(),state,hud_view,hud_input.pointer(),hover_enabled,time);
                else party_hover.reset();
                if(player_tooltip) {
                    // PartyPanel.onMouseOver shows no tooltip while its menu is open.
                    const auto target=hover_enabled&&!(player_menu&&player_menu->visible())&&!(player_group&&player_group->visible()) ? realm::player_tooltip_target(world,party_hover.target()) : std::nullopt;
                    if(player_tooltip->update(target))player_tooltip_texture.update(player_tooltip->image());
                }
                if(!state.inventory.backpack&&hud_input.selected()==2)hud_input.cancel(true);
                if(state.inventory.container!=hud_container) { hud_input.container_changed();hud_container=state.inventory.container; }
                state.inventory.selected=hud_input.selected();state.inventory.dragging=hud_input.dragging();
                state.retain(hud_view);state.sellable.enabled=!game_session||!game_session->buy_pending();
                // HUDModel.getButtonType: the Nexus has the Options button, every other map the Nexus button.
                state.header.button=game_session&&game_session->game_id()!=-1 ? realm::HudState::Header::Button::nexus : realm::HudState::Header::Button::options;
                state.header.hover=!trade.active()&&tooltip_pointer_valid&&!hud_mouse_down&&!options_screen.visible()&&!dialog.visible()&&!board.visible()&&!members.visible()&&!(skins&&skins->visible())&&!interaction.visible()
                    &&realm::HudInput::button_at(realm::header_button,hud_input.pointer());
                const auto panel_button=state.panel_button(fonts);
                const bool hovered=panel_button.id>=0&&realm::HudInput::button_at(panel_button.area,hud_input.pointer());
                state.portal.hover=hovered&&state.portal.id==panel_button.id;state.button.hover=hovered&&state.button.id==panel_button.id;
                state.sellable.hover=hovered&&state.sellable.id==panel_button.id;
                state.social.hover=hovered&&state.social.id==panel_button.id;
                if(hud->update(state))for(const auto& area:hud->changes())hud_texture.update_region(hud->image(),area.x,area.y,area.width,area.height);
                hud_view=state;
                if(tooltip) {
                    // EquipmentToolTip for trade slots replaces the hidden HUD grids.
                    std::optional<realm::ItemTooltipTarget> target;
                    if(tooltip_pointer_valid&&!dialog.visible()&&!board.visible() && !members.visible()&&!(skins&&skins->visible())&&!hud_mouse_down&&!interaction.visible()) {
                        const auto pointer=hud_input.pointer();
                        if(trade.active()&&pointer.y>=realm::TradeView::panel_top) {
                            int side=0,slot=realm::TradeView::slot_at(pointer);
                            if(slot<0) { side=1;slot=realm::TradeView::their_slot_at(pointer); }
                            const auto& items=(side==0 ? trade.mine() : trade.theirs()).items;
                            if(slot>=0&&items.at(static_cast<std::size_t>(slot)).item>0)target=realm::item_tooltip_target(world,realm::InventoryItem{items.at(static_cast<std::size_t>(slot)).item,-1},realm::TooltipOwner::container);
                        } else if(!(trade.request()&&pointer.y>=realm::TradeView::request_top))target=realm::item_tooltip_target(world,state,pointer);
                    }
                    fonts.set_pixel_scale(std::min(height/600.0,(width-8.0)/realm::ItemTooltip::width));
                    if(tooltip->update(target))tooltip_texture.update(tooltip->image());
                    fonts.set_pixel_scale(height/600.0);
                }
                const double ui_scale=height/600.0;
                overlay={};
                // MapOverlay balloons sit beneath the HUD, chat and popups, in map pixels.
                fonts.set_pixel_scale(zoom);balloons.update(world,fonts,time);fonts.set_pixel_scale(height/600.0);
                if(settings.flag("textBubbles"))for(const auto& balloon:balloons.balloons()) {
                    const auto* speaker=world.entity(balloon.object_id);if(!speaker)continue;
                    auto& texture=balloon_textures.at(balloon.slot);
                    if(balloon_uploaded.at(balloon.slot)!=balloon.revision) { texture.update(balloon.image);balloon_uploaded.at(balloon.slot)=balloon.revision; }
                    const auto at=realm::SpeechBalloons::position(balloon,camera.world_to_screen(speaker->position),speaker->size,zoom);
                    const double w=balloon.image.width/balloon.image.pixel_scale*zoom,h=balloon.image.height/balloon.image.pixel_scale*zoom;
                    overlay_storage[overlay.size()]=texture.quad(at,{w,0},{0,h},0,0,balloon.image.width,balloon.image.height);
                    overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                }
                // CharacterStatusText: damage, conditions, notifications and level/exp/fame text float above the balloons.
                for(auto& pending:world.take_status_events())status_texts.add(world,std::move(pending));
                // Fixtures with a fixed effect clock age their text by --status-age after the first frame.
                const std::int32_t status_time=o.effect_time>=0 ? o.effect_time+(frame>0 ? o.status_age : 0) : time;
                fonts.set_pixel_scale(zoom);status_texts.update(world,status_art,status_time);fonts.set_pixel_scale(height/600.0);
                for(const auto& text:status_texts.texts()) {
                    if(!realm::StatusTexts::visible(text,status_time)||!text.label.image||overlay.size()>=100)continue;
                    const auto* speaker=world.entity(text.object_id);if(!speaker)continue;
                    const auto at=realm::StatusTexts::position(text,camera.world_to_screen(speaker->position),speaker->size,zoom,status_time);
                    const double w=text.label.image->width/text.label.image->pixel_scale*zoom,h=text.label.image->height/text.label.image->pixel_scale*zoom;
                    const auto placed=scene.status_quad(text.label.image,atlas,at,{w,0},{0,h});
                    std::shared_ptr<realm::SurfaceTexture> surface;if(!placed)surface=scene.status_surface(text.label.image);
                    auto quad=placed ? *placed : surface->quad(at,{w,0},{0,h});quad.owner=surface;
                    overlay_storage[overlay.size()]=quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                }
                if(chat_view) {
                    if(chat_view->update(chat,time)&&!chat_view->empty())chat_texture.update(chat_view->image());
                    if(!chat_view->empty()) {
                        const auto& image=chat_view->image();
                        auto chat_quad=chat_texture.quad({0,chat_view->top()*ui_scale},{realm::ChatView::width*ui_scale,0},{0,chat_view->surface_height()*ui_scale},0,0,image.width,image.height);
                        overlay_storage[overlay.size()]=chat_quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                    }
                }
                auto q=hud_texture.quad({width-200*ui_scale,0},{200*ui_scale,0},{0,static_cast<double>(height)});q.kind=5;overlay_storage[overlay.size()]=q;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                if(minimap) {
                    if(minimap->update(world,*player,angle))minimap_texture.update(minimap->image());
                    auto map_quad=minimap_texture.quad({width-196*ui_scale,4*ui_scale},{192*ui_scale,0},{0,192*ui_scale});map_quad.kind=5;overlay_storage[overlay.size()]=map_quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                }
                if(trade_view) {
                    trade.update(time);
                    if(trade_view->update(trade,hud_input.pointer(),time)) {
                        if(trade.active())trade_texture.update(trade_view->panel());
                        else if(trade.request())trade_request_texture.update(trade_view->request());
                    }
                    if(trade.active()||trade.request()) {
                        const bool panel=trade.active();const auto& image=panel ? trade_view->panel() : trade_view->request();
                        const double top=panel ? realm::TradeView::panel_top : realm::TradeView::request_top,h=panel ? realm::TradeView::panel_height : realm::TradeView::request_height;
                        auto trade_quad=(panel ? trade_texture : trade_request_texture).quad({width-200*ui_scale,top*ui_scale},{200*ui_scale,0},{0,h*ui_scale},0,0,image.width,image.height);
                        overlay_storage[overlay.size()]=trade_quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                    }
                }
                if(hud_art&&hud_input.dragging()>=0) {
                    const auto slot=static_cast<std::size_t>(hud_input.dragging());const int type=state.inventory.items.at(slot);
                    if(type>0) {
                        const auto key=(slot<4&&state.inventory.dim.at(slot)?"dim":"item")+std::to_string(type);
                        if(drag_key!=key) {
                            const auto& icon=hud_art->image(key);if(icon.width>128||icon.height>128)throw std::runtime_error("Drag icon exceeds reserved image");
                            std::fill(drag_bitmap.pixels.begin(),drag_bitmap.pixels.end(),0);
                            for(int y=0;y<icon.height;++y)for(int x=0;x<icon.width;++x) {
                                const auto src=(static_cast<std::size_t>(y)*static_cast<std::size_t>(icon.width)+static_cast<std::size_t>(x))*4;
                                const auto dst=(static_cast<std::size_t>(y+64-icon.height/2)*128+static_cast<std::size_t>(x+64-icon.width/2))*4;
                                const unsigned alpha=icon.pixels[src+3];drag_bitmap.pixels[dst+3]=static_cast<unsigned char>(alpha);
                                for(unsigned c=0;c<3;++c)drag_bitmap.pixels[dst+c]=static_cast<unsigned char>(alpha==255?icon.pixels[src+c]:alpha?static_cast<unsigned>(icon.pixels[src+c])*256/alpha:0);
                            }
                            atlas.update_additional(drag_index,drag_bitmap);drag_key=key;
                        }
                        const auto pointer=hud_input.pointer();auto drag=realm::sprite_quad(atlas.additional(drag_index),{width+(pointer.x-264)*ui_scale,(pointer.y-64)*ui_scale},{128*ui_scale,0},{0,128*ui_scale});drag.kind=5;overlay_storage[overlay.size()]=drag;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                    }
                }
                if(tooltip&&tooltip->visible()) {
                    const auto pointer=hud_input.pointer();const realm::Vec2 screen_pointer{width+(pointer.x-200)*ui_scale,pointer.y*ui_scale};
                    const double scale=std::min(ui_scale,(width-8.0)/realm::ItemTooltip::width);
                    const auto at=realm::ItemTooltip::position(screen_pointer,width,height,tooltip->height(),scale);
                    auto tip=tooltip_texture.quad(at,{realm::ItemTooltip::width*scale,0},{0,tooltip->height()*scale},0,0,tooltip->image().width,static_cast<int>(std::ceil(tooltip->height()*tooltip->image().pixel_scale)));
                    overlay_storage[overlay.size()]=tip;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                }
                if(player_tooltip&&player_tooltip->visible()) {
                    const auto pointer=hud_input.pointer();const realm::Vec2 screen_pointer{width+(pointer.x-200)*ui_scale,pointer.y*ui_scale};
                    const double scale=ui_scale;
                    const auto at=realm::PlayerTooltip::position(screen_pointer,width,height,player_tooltip->draw_width(),player_tooltip->height(),scale);
                    const double density=player_tooltip->image().pixel_scale;
                    auto tip=player_tooltip_texture.quad(at,{player_tooltip->draw_width()*scale,0},{0,player_tooltip->height()*scale},0,0,static_cast<int>(std::ceil(player_tooltip->draw_width()*density)),static_cast<int>(std::ceil(player_tooltip->height()*density)));
                    overlay_storage[overlay.size()]=tip;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                }
            }
            if(player_group&&hud&&minimap&&o.world&&!menus) {
                const auto pointer=hud_input.pointer();const double scale=height/600.0;
                const realm::Vec2 screen_pointer{width+(pointer.x-200)*scale,pointer.y*scale};
                const bool enabled=!dialog.visible()&&!board.visible()&&!members.visible()&&!(skins&&skins->visible())&&!interaction.visible()&&game_session&&game_session->state()==realm::SessionState::playing;
                if(!enabled) { player_group->close();party_arrows.reset(); }
                else {
                    party_arrows.update(world,game_session->party(),camera,screen_pointer,tooltip_pointer_valid,width,height);
                    for(std::size_t i=0;i<party_arrows.arrows().size();++i) {
                        const auto& arrow=party_arrows.arrows()[i];if(arrow.players.empty())continue;
                        const double density=std::max(1.0,camera.scale());
                        if(arrow_densities[i]!=density||arrow_rotations[i]!=arrow.rotation||arrow_hovers[i]!=arrow.hover) {
                            arrow_textures[i].update(realm::PartyArrows::image(arrow.rotation,arrow.hover,density));
                            arrow_densities[i]=density;arrow_rotations[i]=arrow.rotation;arrow_hovers[i]=arrow.hover;
                        }
                        const double a_scale=camera.scale();
                        const auto quad=arrow_textures[i].quad({arrow.position.x-realm::PartyArrows::origin*a_scale,arrow.position.y-realm::PartyArrows::origin*a_scale},{realm::PartyArrows::canvas_size*a_scale,0},{0,realm::PartyArrows::canvas_size*a_scale});
                        overlay_storage[overlay.size()]=quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                    }
                }
                if(enabled&&player_group->menu()) {
                    if(player_group->refresh_appearance(world))group_texture.update(player_group->image());
                    if(player_group->update(screen_pointer,tooltip_pointer_valid,game_session->teleport_cooldown(time),scale))group_texture.update(player_group->image());
                } else if(enabled&&party_arrows.hovered()&&!(player_menu&&player_menu->visible())) {
                    const auto& arrow=*party_arrows.hovered();
                    fonts.set_pixel_scale(scale*camera.scale());
                    if(player_group->arrow_tooltip(realm::player_group_targets(world,arrow.players),arrow.position,arrow.rotation,camera.scale(),scale*camera.scale()))group_texture.update(player_group->image());
                    fonts.set_pixel_scale(scale);
                } else if(enabled)if(const auto* focus=world.entity(world.player_id())) {
                    const auto ids=(!tooltip_pointer_valid||(player_menu&&player_menu->visible())) ? std::vector<std::int32_t>{} : minimap->hover_players(world,*focus,{pointer.x-4,pointer.y-4});
                    if(player_group->tooltip(realm::player_group_targets(world,ids),screen_pointer,width,height,scale))group_texture.update(player_group->image());
                }
                if(player_group->visible()) {
                    const auto& image=player_group->image();
                    auto quad=group_texture.quad(player_group->canvas_position(),{player_group->canvas_width()*player_group->scale(),0},{0,player_group->canvas_height()*player_group->scale()},0,0,image.width,image.height);
                    overlay_storage[overlay.size()]=quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                }
            }
            if(player_menu&&player_menu->visible()&&hud&&o.world&&!menus) {
                const auto pointer=hud_input.pointer();const double ui_scale=height/600.0;
                const realm::Vec2 screen_pointer{width+(pointer.x-200)*ui_scale,pointer.y*ui_scale};
                const bool valid=tooltip_pointer_valid&&!dialog.visible()&&!board.visible() && !members.visible()&&!(skins&&skins->visible())&&!interaction.visible();
                if(player_menu->update(world,screen_pointer,valid,game_session ? game_session->teleport_cooldown(time) : 0,ui_scale))player_menu_texture.update(player_menu->image());
                if(player_menu->visible()) {
                    const auto& image=player_menu->image();const double scale=player_menu->scale();
                    auto menu_quad=player_menu_texture.quad(player_menu->canvas_position(),{realm::PlayerMenu::canvas_width*scale,0},{0,realm::PlayerMenu::canvas_height*scale},0,0,image.width,image.height);
                    overlay_storage[overlay.size()]=menu_quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                }
            }
            if(interaction.visible()&&!menus) {
                fonts.set_pixel_scale(realm::InteractionDialog::scale(width,height));
                if(interaction.render(fonts))interaction_texture.update(interaction.image());
                fonts.set_pixel_scale(height/600.0);
                const auto at=realm::InteractionDialog::position(width,height);const double scale=realm::InteractionDialog::scale(width,height);
                auto dim=realm::Quad{};dim.origin={0,0};dim.axis_x={static_cast<float>(width),0};dim.axis_y={0,static_cast<float>(height)};dim.kind=2;dim.multiply={0,0,0,.55F};
                overlay_storage[overlay.size()]=dim;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                auto frame_quad=interaction_texture.quad(at,{realm::InteractionDialog::width*scale,0},{0,realm::InteractionDialog::height*scale});frame_quad.kind=5;
                overlay_storage[overlay.size()]=frame_quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
            }
            if(board.visible()&&!menus) {
                if(board.update(fonts,width,height))board_texture.update(board.image());
                if(board.backdrop()) {
                    auto dim=realm::Quad{};dim.origin={0,0};dim.axis_x={static_cast<float>(width),0};dim.axis_y={0,static_cast<float>(height)};dim.kind=2;dim.multiply={0,0,0,.8F};
                    overlay_storage[overlay.size()]=dim;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                }
                const auto& image=board.image();
                auto frame_quad=board_texture.quad(board.canvas_position(),{image.width*board.scale()/image.pixel_scale,0},{0,image.height*board.scale()/image.pixel_scale});
                overlay_storage[overlay.size()]=frame_quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
            }
            if(members.visible()&&!menus) {
                if(members.update(fonts,width,height))members_texture.update(members.image());
                auto quad=members_texture.quad({0,0},{static_cast<double>(width),0},{0,static_cast<double>(height)});
                overlay_storage[overlay.size()]=quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
            }
            if(skins&&skins->visible()&&!menus) {
                if(skins->update(width,height,time))skin_texture.update(skins->image());
                auto quad=skin_texture.quad({0,0},{static_cast<double>(width),0},{0,static_cast<double>(height)});overlay_storage[overlay.size()]=quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
            }
            if(options_screen.visible()&&!menus) {
                if(options_screen.update(fonts,width,height,time))options_texture.update(options_screen.image());
                auto quad=options_texture.quad({0,0},{static_cast<double>(width),0},{0,static_cast<double>(height)});overlay_storage[overlay.size()]=quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
            }
            if(perf_stats&&o.world&&!menus&&!fonts.empty()) {
                // Toggle Performance Stats: frame rate, frame time, submit time and resident memory.
                if(perf_dirty&&!perf_text.empty()) {
                    const double ui=height/600.0;
                    std::vector<std::string> lines;std::istringstream in(perf_text);for(std::string line;std::getline(in,line);)lines.push_back(line);
                    std::vector<realm::TextBitmap> rows;double box_w=0,box_h=0;
                    for(const auto& line:lines) { rows.push_back(fonts.rasterize(line,12,true,true,0xffffff));box_w=std::max(box_w,rows.back().width);box_h+=rows.back().height; }
                    auto image=realm::ui_image(box_w+8,box_h+8,ui);double row_y=4;
                    for(const auto& row:rows) { realm::ui_blit_text(image,row,4+row.offset.x,row_y+row.offset.y,false,false);row_y+=row.height; }
                    perf_size={static_cast<double>(image.width),static_cast<double>(image.height)};perf_texture.update(image);perf_dirty=false;
                }
                if(!perf_text.empty()) {
                    auto quad=perf_texture.quad({4.0*height/600,4.0*height/600},{perf_size.x,0},{0,perf_size.y});
                    overlay_storage[overlay.size()]=quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                }
            }
            if(hud&&o.world&&!menus&&hud_view.header.hover&&!options_screen.visible()) {
                // IconButton: TextToolTip(0x363636, 0x9B9B9B, title, "Hotkey: KEY", 200) at the pointer.
                const bool nexus=hud_view.header.button==realm::HudState::Header::Button::nexus;
                const std::string title=nexus ? "Nexus" : "Options",text="Hotkey: "+std::string(realm::key_name(settings.get(nexus ? "escapeToNexus" : "options")));
                const auto key=title+"|"+text+"|"+std::to_string(height);
                if(key!=header_tip_key) { header_tip=realm::render_text_tooltip(fonts,title,text,3552822,10197915,200);header_tip_texture.update(header_tip.image);header_tip_key=key; }
                const double ui=height/600.0;const auto pointer=hud_input.pointer();
                const auto at=realm::text_tooltip_position({width+(pointer.x-200)*ui,pointer.y*ui},header_tip.frame_width,header_tip.frame_height,width,height,ui);
                const auto& image=header_tip.image;
                auto quad=header_tip_texture.quad({at.x-header_tip.margin*ui,at.y-header_tip.margin*ui},{static_cast<double>(image.width),0},{0,static_cast<double>(image.height)},0,0,image.width,image.height);
                overlay_storage[overlay.size()]=quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
            }
            if(dialog.visible()&&!menus) {
                if(dialog.update(fonts,width,height))dialog_texture.update(dialog.image());
                auto dim=realm::Quad{};dim.origin={0,0};dim.axis_x={static_cast<float>(width),0};dim.axis_y={0,static_cast<float>(height)};dim.kind=2;dim.multiply={21/255.F,21/255.F,21/255.F,.6F};
                overlay_storage[overlay.size()]=dim;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                const auto& image=dialog.image();
                auto frame_quad=dialog_texture.quad(dialog.canvas_position(),{image.width*dialog.scale()/image.pixel_scale,0},{0,image.height*dialog.scale()/image.pixel_scale});
                overlay_storage[overlay.size()]=frame_quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
            }
            realm::ScreenEffects effects;
            if(o.world&&(game_session||!o.map.empty())) {
                const realm::Entity* local=world.entity(world.player_id());
                effects=realm::screen_effects(local,o.effect_time>=0?o.effect_time:time,height/600.0);
            }
            ui_zone.finish();
            const bool capture_frame=(!o.capture.empty()&&(!o.frames?frame==0:frame+1==static_cast<std::uint64_t>(o.frames)))||!script_capture.empty();
            const bool normal_frame=frame>=static_cast<std::uint64_t>(o.telemetry_warmup)&&!capture_frame;
            renderer.timing(telemetry.get(),frame,normal_frame);
            renderer.draw(atlas, quads, width, height, clip_width,overlay,effects);
            const auto submitted = Clock::now();cpu_zone.finish();

            // Capture before swap; finite runs always capture their last rendered frame.
            if (!o.capture.empty() && (!o.frames ? frame == 0 : frame + 1 == static_cast<std::uint64_t>(o.frames)))
                realm::save_png(o.capture, renderer.capture(width, height));
            if (!script_capture.empty()) {
                realm::save_png(script_capture, renderer.capture(width, height));
                if(o.benchmark)std::cout<<"Particle capture "<<script_capture.filename()<<" particles="<<world.particles().particles().size()<<" emitters="<<world.particles().emitter_count()<<" flashes="<<world.particles().flashes().size()<<" dropped="<<world.particles().dropped()<<" quads="<<quads.size()<<'\n';
            }
            const auto present_start=Clock::now();
            { REALM_ZONE("present.swap");SDL_GL_SwapWindow(session.window); }
            const auto presented = Clock::now();
            const int warmup = o.frames > 60 ? 30 : 0;
            if (o.frames && frame >= static_cast<std::uint64_t>(warmup) && !capture_frame) {
                cpu_samples.push_back(milliseconds(submitted - start));
                present_samples.push_back(milliseconds(presented - present_start));
            }
            if (frame % 60 == 0 && !startup) {
                const std::string title = "Realm C++ harness | " + std::string(sheet.name) + (o.world ? (o.map.empty() ? " | camera fixture" : " | " + world.name()) : " | raw asset gallery") +
                    " | quads=" + std::to_string(quads.size()+overlay.size()) + " draws=" + std::to_string(renderer.draw_calls());
                SDL_SetWindowTitle(session.window, title.c_str());
            }
            if (o.frames && frame + 1 == static_cast<std::uint64_t>(o.frames))
                std::cout << "Last frame quads=" << quads.size()+overlay.size() << " draws=" << renderer.draw_calls() << " GPU_last_ms=" << renderer.gpu_ms() << '\n';
            if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL error during frame");
            if (perf_stats) {
                ++perf_frames;perf_clock+=dt*1000;perf_cpu+=milliseconds(submitted-start);perf_worst=std::max(perf_worst,dt*1000);
                if (perf_clock>=500) {
                    std::ostringstream text;text<<std::fixed<<std::setprecision(1)<<"FPS "<<perf_frames*1000/perf_clock<<"   frame "<<perf_clock/perf_frames<<" ms (worst "<<perf_worst<<")\n"
                        <<"CPU "<<perf_cpu/perf_frames<<" ms   draws "<<renderer.draw_calls();
                    if (const double mb=resident_megabytes();mb>0)text<<"   memory "<<std::setprecision(0)<<mb<<" MB";
                    perf_text=text.str();perf_dirty=true;perf_frames=0;perf_clock=perf_cpu=perf_worst=0;
                }
            }
            // Max FPS: VSync alone does not cap a fast display or an unsynchronized swap.
            if (!o.benchmark && !o.hidden && settings.get("maxFPS") > 0) {
                const double target = 1000.0 / settings.get("maxFPS");
                while (milliseconds(Clock::now() - start) < target) {
                    if (target - milliseconds(Clock::now() - start) > 2) SDL_Delay(1);
                }
            }
            if(telemetry) {
                using C=realm::telemetry::Counter;
                realm::telemetry::set(C::resident_entities,world.entities().size());
                realm::telemetry::set(C::resident_projectiles,game_session?game_session->projectile_count():stress?stress->projectile_count():0);
                realm::telemetry::set(C::outgoing_queue_bytes,game_session?game_session->outgoing_queue_bytes():0);
                realm::telemetry::set(C::framing_queue_bytes,game_session?game_session->framing_queue_bytes():0);
                realm::telemetry::set(C::particles,world.particles().particles().size());
                realm::telemetry::set(C::appearance_cpu_bytes,appearances.bytes());
                realm::telemetry::set(C::appearance_gpu_bytes,scene.appearance_bytes());
                realm::telemetry::set(C::glyph_bytes,fonts.cache_bytes());
                const auto drops=world.particles().dropped();
                realm::telemetry::set(C::optional_drops,drops>=previous_drops?drops-previous_drops:drops);
                // Capture/readback is excluded from CPU/present and marked on the
                // frame. The following interval also includes capture work.
                telemetry->end({frame,frame<static_cast<std::uint64_t>(o.telemetry_warmup),capture_frame,frame>0&&!capture_frame&&!previous_capture,
                    frame_interval,milliseconds(submitted-start),milliseconds(presented-present_start),milliseconds(Clock::now()-presented),
                    input_observed?std::max(0.0,input_age+milliseconds(submitted-start)):-1,width,height,SDL_GL_GetSwapInterval(),o.benchmark||o.hidden?0:settings.get("maxFPS")});
            }
            if(stress){stress->sample_scene(camera,width,height,quads,scene.submitted_counts());
                const double rss=(frame%50==0||frame+1==static_cast<std::uint64_t>(o.frames))?resident_megabytes():0;
                stress->sample_resources(rss,appearances.entries(),appearances.bytes(),scene.appearance_entries(),scene.appearance_bytes());}
            if((stress||o.fixture_step)&&o.stress_delay)SDL_Delay(static_cast<Uint32>(o.stress_delay));
            previous_capture=capture_frame;
#ifdef REALM_TRACY
            FrameMark;
#endif
            ++frame;
        }
        if(telemetry) {
            renderer.poll_gpu();
            telemetry->metadata("gpu_pending_at_exit",std::to_string(renderer.pending_gpu()));
            telemetry->metadata("gpu_skipped_busy_slots",std::to_string(renderer.skipped_gpu()));
            telemetry->finish();
        }
        if(stress)stress->write_summary(o.stress_summary);
        // Parameters.cameraAngle persists across sessions.
        if (persist_settings && settings.camera_angle() != angle) save_settings();
        std::cout << "Appearance CPU=" << appearances.entries() << "/" << appearances.bytes() << " GPU=" << scene.appearance_entries() << "/" << scene.appearance_bytes() << " evictions=" << appearances.evictions() << '\n';
        std::cout << "Dynamic atlas pages=" << atlas.dynamic_pages() << "/" << atlas_maximum << " resizes=" << atlas.resizes() << " resident=" << scene.residency().entries() << "/" << scene.residency().used_bytes() << " evictions=" << scene.residency().evictions() << " fallbacks=" << scene.atlas_fallbacks() << '\n';
        std::cout << std::fixed << std::setprecision(4);
        report("CPU event/build/submit", std::move(cpu_samples)); report("Present", std::move(present_samples));
        return 0;
    } catch (const std::exception& error) { std::cerr << "realm_viewer: " << error.what() << '\n'; return 1; }
}
