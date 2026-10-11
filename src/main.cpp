#include "realm/scene.hpp"
#include "realm/session.hpp"
#include "realm/movement.hpp"
#include "realm/font.hpp"
#include "realm/hud.hpp"
#include "realm/hud_input.hpp"
#include "realm/tooltip.hpp"
#include "realm/player_tooltip.hpp"
#include "realm/interaction.hpp"
#include "realm/minimap.hpp"
#include "realm/startup.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <algorithm>
#include <charconv>
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

#ifndef REALM_CLIENT_DEFAULT
#define REALM_CLIENT_DEFAULT 0
#endif
namespace {
using Clock = std::chrono::steady_clock;
struct Options {
    std::filesystem::path root{realm::flash::source_root}, capture, map, processed, texture, fonts, hud_art, account_file, script;
    bool client{REALM_CLIENT_DEFAULT != 0};
    std::uint16_t app_port{7777};
    std::string text;int font_size{14};bool font_bold{},font_shadow{},font_yhalf{};
    std::string sheet{"lofiEnvironment"};
    int width{800}, height{600}, frames{}, sprites{}, scale{4};
    bool hidden{}, benchmark{}, world{}, offset{}, connect{}, password_stdin{}, projectile_fixture{}, projectile_outline{};
    double projectile_angle{}; int projectile_size{100}; bool raw_flash_bitmap{};
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
        else if (arg == "--capture") o.capture = value();
        else if (arg == "--hidden") o.hidden = true;
        else if (arg == "--benchmark") o.benchmark = true;
        else if (arg == "--world") o.world = true;
        else if (arg == "--offset") o.offset = true;
        else if (arg == "--help") {
            std::cout << "realm_viewer [--flash-root PATH] [--sheet NAME] [--width N] [--height N]\n"
                "  [--scale N] [--map PATH.jm|PATH.wmap] [--world] [--offset] [--frames N] [--sprites N]\n"
                "  [--processed DIR] [--fonts DIR] [--hud-art DIR] [--capture PATH.png] [--hidden] [--benchmark]\n"
                "Gallery: Left/Right select set; Up/Down page. Tab toggles camera fixture.\n"
                "Camera fixture: WASD pan; Q/E rotate; +/- zoom; C toggle camera centering. Escape exits.\n"
                "Live session: --connect HOST --port N --username NAME --password-stdin --character N\n"
                "Use --create CLASS_TYPE to create a character instead of loading one.\n"
                "Inventory: drag to swap/drop; double-click equip/use; Shift-click use; Ctrl-click backpack transfer; 1-8 use.\n"
                "Potions: F/V or Shift/double-click the health/magic stacks.\n"
                "Portals: click Enter or press 0 beside a portal.\n"
                "Client: --client [--server HOST] [--port N] [--app-port N] [--account-file PATH] runs the\n"
                "  startup sequence: loading, title, character selection, then gameplay (realm_client default).\n"
                "  Escape in game returns to character selection.\n"
                "Input script: --script FILE replays lines 'MS click X Y', 'MS key escape|enter|tab|backspace',\n"
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
    if (o.client) o.world = true;
    if (o.benchmark && o.frames == 0) o.frames = 600;
    if (o.hidden && o.frames == 0 && o.script.empty()) throw std::invalid_argument("--hidden requires a finite --frames count or a --script");
    return o;
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
        if (step.action == "click" || step.action == "move") { if (!(in >> step.x >> step.y)) throw std::invalid_argument("Invalid pointer input: " + line); }
        else if (step.action == "key" || step.action == "text" || step.action == "capture") {
            std::getline(in >> std::ws, step.argument);
            if (step.argument.empty()) throw std::invalid_argument("Invalid script argument: " + line);
        } else if (step.action != "quit") throw std::invalid_argument("Unknown script action: " + line);
        steps.push_back(std::move(step));
    }
    std::stable_sort(steps.begin(), steps.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    return steps;
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
            o.width, o.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (o.hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN));
        if (!session.window) throw std::runtime_error(SDL_GetError());
        session.context = SDL_GL_CreateContext(session.window);
        if (!session.context) throw std::runtime_error(SDL_GetError());
        glewExperimental = GL_TRUE;
        const auto glew_status = glewInit();
        if (glew_status != GLEW_OK) throw std::runtime_error(reinterpret_cast<const char*>(glewGetErrorString(glew_status)));
        // GLEW can leave GL_INVALID_ENUM on core contexts during extension discovery.
        while (glGetError() != GL_NO_ERROR) {}
        if (!GLEW_VERSION_3_3) throw std::runtime_error("OpenGL 3.3 required");
        if (SDL_GL_SetSwapInterval(o.benchmark ? 0 : 1) != 0)
            std::cerr << "Swap interval unavailable: " << SDL_GetError() << '\n';
        std::cout << "Flash revision " << realm::flash::revision << "\nGPU " << glGetString(GL_RENDERER)
                  << "\nGL " << glGetString(GL_VERSION) << '\n';
        realm::AnimationLibrary animations(o.root);
        realm::ProcessedTextures processed(o.processed, animations.images().size());
        auto atlas_images = animations.images();
        atlas_images.insert(atlas_images.end(), processed.images().begin(), processed.images().end());
        realm::Catalog catalog(o.root);realm::FontBook fonts(o.fonts);
        std::unique_ptr<realm::HudArt> hud_art;if(!o.hud_art.empty())hud_art=std::make_unique<realm::HudArt>(o.hud_art);
        std::unique_ptr<realm::Hud> hud;
        const auto hud_index=atlas_images.size();
        if(o.world&&!fonts.empty()) { hud=std::make_unique<realm::Hud>(fonts,hud_art.get());atlas_images.push_back(hud->image()); }
        std::unique_ptr<realm::Minimap> minimap;
        const auto minimap_index=atlas_images.size();
        if(hud) { minimap=std::make_unique<realm::Minimap>(catalog,animations,o.root);atlas_images.push_back(minimap->image()); }
        const auto drag_index=atlas_images.size();realm::Image drag_bitmap{128,128,std::vector<unsigned char>(128*128*4)};
        if(hud_art)atlas_images.push_back(drag_bitmap);
        std::unique_ptr<realm::ItemTooltip> tooltip;
        const auto tooltip_index=atlas_images.size();
        if(hud&&hud_art) { tooltip=std::make_unique<realm::ItemTooltip>(catalog,fonts,*hud_art);atlas_images.push_back(tooltip->image()); }
        realm::PartyHover party_hover;
        std::unique_ptr<realm::PlayerTooltip> player_tooltip;
        const auto player_tooltip_index=atlas_images.size();
        if(hud&&hud_art) { player_tooltip=std::make_unique<realm::PlayerTooltip>(catalog,fonts,*hud_art);atlas_images.push_back(player_tooltip->image()); }
        realm::InteractionDialog interaction;
        const auto interaction_index=atlas_images.size();
        if(hud)atlas_images.push_back(interaction.image());
        bool tooltip_pointer_valid{},hud_mouse_down{};
        realm::HudInput hud_input;std::string drag_key;std::int32_t hud_container=-1;
        // The interaction panel as last drawn; clicks hit what is on screen.
        realm::HudState hud_view;
        realm::TextBitmap text_fixture;
        const auto text_fixture_index=atlas_images.size();
        if(!o.text.empty()) { text_fixture=fonts.rasterize(o.text,o.font_size,o.font_bold,o.font_shadow,0xffffff,0,o.font_yhalf?.5:0);atlas_images.push_back(text_fixture.image); }
        realm::World world(catalog); realm::Scene scene;
        std::unique_ptr<realm::Session> game_session;
        std::unique_ptr<realm::Startup> startup;
        const auto startup_index=atlas_images.size();
        // World holds MapBackground's map until a session's MAPINFO replaces it.
        bool background_world{};
        if (o.client) {
            if (fonts.empty()) throw std::runtime_error("The client needs baked fonts (--fonts)");
            startup = std::make_unique<realm::Startup>(fonts, hud_art.get(), catalog, o.root,
                realm::StartupOptions{o.session.host, o.session.build_version, o.app_port, o.session.port, o.account_file});
            atlas_images.push_back(startup->sheet());
            startup->load_background(world); background_world = true;
        }
        if (!o.map.empty()) {
            if (o.map.extension() == ".wmap") world.load_wmap(o.map); else world.load_jm(o.map);
            const auto spawn = world.spawn_position();
            if (const auto* player = catalog.object("Wizard")) { world.add(-2, player->type, spawn); world.set_player_id(-2); auto* local = world.entity(-2); local->name="Wizard";local->stats[static_cast<std::uint8_t>(realm::flash::StatId::MP_STAT)]=player->max_mp;local->stats[static_cast<std::uint8_t>(realm::flash::StatId::MAX_MP_STAT)]=player->max_mp;local->stats[static_cast<std::uint8_t>(realm::flash::StatId::NEXT_LEVEL_EXP_STAT)]=50; local->speed = player->xml.integer("Speed"); local->attack = player->xml.integer("Attack"); local->dexterity = player->xml.integer("Dexterity");
                auto equipment=player->xml.child("Equipment").text();std::replace(equipment.begin(),equipment.end(),',',' ');std::istringstream slots(equipment);std::string item;int slot{};while(slot<20&&slots>>item)realm::set_inventory_item(*local,slot++,{std::stoi(item,nullptr,0),-1}); }
            std::cout << "Map " << world.name() << " " << world.width() << "x" << world.height() << " objects=" << world.entities().size() << " unresolved=" << world.unresolved_ids().size() << '\n';
        }
        std::unique_ptr<realm::Terrain> terrain;
        const auto terrain_first = atlas_images.size();
        if (!o.map.empty() || o.connect || o.client) {
            terrain = std::make_unique<realm::Terrain>(world, o.root, terrain_first, -1, o.connect || o.client);
            atlas_images.insert(atlas_images.end(), terrain->images().begin(), terrain->images().end());
            std::cout << "Terrain variants=" << terrain->images().size() << '\n';
        }
        const auto fixture_index = atlas_images.size();
        if (!o.texture.empty()) { auto image=realm::load_png(o.texture); if(o.raw_flash_bitmap) realm::normalize_flash_png(image); atlas_images.push_back(std::move(image)); }
        realm::Atlas atlas(o.root, atlas_images, animations.images().size());
        if (o.connect) {
            if (o.password_stdin) std::getline(std::cin,o.session.password);
            game_session = std::make_unique<realm::Session>(world); game_session->connect(std::move(o.session));
        }
        constexpr std::size_t base_capacity = 4096;
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
        realm::Camera camera; realm::Vec2 position = o.map.empty() ? realm::Vec2{} : world.spawn_position();
        double angle = 7 * std::numbers::pi / 4, zoom = 1;
        bool centered = !o.offset, running = true, leaving_game = false;
        std::vector<realm::Quad> menu_quads;
        const auto script = o.script.empty() ? std::vector<ScriptStep>{} : load_script(o.script);
        std::size_t script_next{};
        int page{}; std::uint64_t frame{}; auto previous = Clock::now(); const auto epoch = previous;
        while (running && (!o.frames || frame < static_cast<std::uint64_t>(o.frames))) {
            const auto start = Clock::now();
            const double dt = std::min(milliseconds(start - previous) / 1000, .1); previous = start;
            const auto time = static_cast<std::int32_t>(milliseconds(start - epoch));
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
                        party_hover.reset();hud_view.party={};tooltip_pointer_valid=false;
                    }
                    interaction.receive(message);
                    if(auto name=interaction.take_name();name&&startup)startup->renamed_account(*name);
                    if (message.id == realm::flash::PacketId::TEXT) std::cout << std::get<realm::net::incoming::Text>(message.data).text << '\n';
                }
                const auto state = game_session->state();
                if (startup) {
                    if (state == realm::SessionState::playing && startup->menus()) startup->entered_game();
                    if (state == realm::SessionState::failed || state == realm::SessionState::disconnected || state == realm::SessionState::dead) {
                        std::string message = state == realm::SessionState::dead ? "Your character has died." : leaving_game ? "" : game_session->error();
                        if (message.empty() && state != realm::SessionState::dead && !leaving_game) message = "Disconnected from the game server.";
                        game_session.reset();interaction.reset();party_hover.reset();hud_view.party={};tooltip_pointer_valid=false; leaving_game = false; hud_input.cancel(); hud_container = -1;
                        startup->left_game(message); startup->load_background(world); background_world = true;
                    }
                } else if (state == realm::SessionState::failed || state == realm::SessionState::disconnected) throw std::runtime_error(game_session->error());
                interaction.update(world);
                if (game_session) if (const auto* player = world.entity(world.player_id())) position = player->position;
            }
            const bool menus = startup && startup->menus();
            if (startup||hud) {
                const bool text = (startup&&startup->wants_text())||interaction.wants_text();
                if (text != (SDL_IsTextInputActive() == SDL_TRUE)) { if (text) SDL_StartTextInput(); else SDL_StopTextInput(); }
            }
            std::filesystem::path script_capture;
            for (; script_next < script.size() && script[script_next].time <= time; ++script_next) {
                const auto& step = script[script_next];
                SDL_Event injected{};
                if (step.action == "move") {
                    injected.type=SDL_MOUSEMOTION;injected.motion.x=step.x;injected.motion.y=step.y;SDL_PushEvent(&injected);
                } else if (step.action == "click") {
                    injected.type = SDL_MOUSEMOTION; injected.motion.x = step.x; injected.motion.y = step.y; SDL_PushEvent(&injected);
                    injected = {}; injected.type = SDL_MOUSEBUTTONDOWN; injected.button.button = SDL_BUTTON_LEFT; injected.button.state = SDL_PRESSED;
                    injected.button.x = step.x; injected.button.y = step.y; SDL_PushEvent(&injected);
                    injected.type = SDL_MOUSEBUTTONUP; injected.button.state = SDL_RELEASED; SDL_PushEvent(&injected);
                } else if (step.action == "key") {
                    const SDL_Keycode key = SDL_GetKeyFromName(step.argument.c_str());
                    if (key == SDLK_UNKNOWN) throw std::invalid_argument("Unknown script key: " + step.argument);
                    injected.type = SDL_KEYDOWN; injected.key.state = SDL_PRESSED; injected.key.keysym.sym = key; injected.key.keysym.scancode = SDL_GetScancodeFromKey(key);
                    SDL_PushEvent(&injected);
                } else if (step.action == "text") {
                    // sdl2-compat crashes converting pushed SDL_TEXTINPUT events.
                    if(interaction.visible())interaction.text(step.argument);else if (startup) startup->text(step.argument);
                } else if (step.action == "capture") script_capture = step.argument;
                else running = false;
            }
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = false;
                if (menus) {
                    int lw{}, lh{}, dw{}, dh{}; SDL_GetWindowSize(session.window, &lw, &lh); SDL_GL_GetDrawableSize(session.window, &dw, &dh);
                    const auto pixels = [&](int x, int y) { return realm::Vec2{lw > 0 ? static_cast<double>(x) * dw / lw : x, lh > 0 ? static_cast<double>(y) * dh / lh : y}; };
                    switch (event.type) {
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
                if(event.type==SDL_WINDOWEVENT&&(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST||event.window.event==SDL_WINDOWEVENT_LEAVE)) {
                    tooltip_pointer_valid=false;
                    if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) { hud_input.cancel();hud_mouse_down=false; }
                }
                if(event.type==SDL_MOUSEMOTION)tooltip_pointer_valid=true;
                if(event.type==SDL_MOUSEBUTTONDOWN&&event.button.button==SDL_BUTTON_LEFT)hud_mouse_down=true;
                if(event.type==SDL_MOUSEBUTTONUP&&event.button.button==SDL_BUTTON_LEFT)hud_mouse_down=false;
                if(event.type==SDL_MOUSEWHEEL&&tooltip&&tooltip->visible()) {
                    const int steps=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
                    if(tooltip->scroll(steps))atlas.update_additional(tooltip_index,tooltip->image());
                }
                if(hud_art&&hud&&o.world&&(event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP||event.type==SDL_MOUSEMOTION))if(const auto* player=world.entity(world.player_id())) {
                    int lw{},lh{};SDL_GetWindowSize(session.window,&lw,&lh);
                    if(lh>0) {
                        const int mx=event.type==SDL_MOUSEMOTION?event.motion.x:event.button.x,my=event.type==SDL_MOUSEMOTION?event.motion.y:event.button.y;
                        const realm::Vec2 point{(mx-lw)/(lh/600.0)+200,my/(lh/600.0)};
                        auto state=realm::HudState::player(world,*player).inventory;
                        if(state.container!=hud_container) { hud_input.container_changed();hud_container=state.container; }
                        if(event.type==SDL_MOUSEMOTION)hud_input.move(point,hud_input.selected(),state.container>=0);
                        else if(event.button.button==SDL_BUTTON_LEFT) {
                            const auto& panel=hud_view;const auto button=panel.panel_button(fonts);
                            if(event.type==SDL_MOUSEBUTTONDOWN)hud_input.button_down(point,button);
                            else if(hud_input.button_up(point,button)&&game_session) {
                                if(panel.portal.id==button.id)game_session->use_portal(button.id);
                                else if(panel.social.id==button.id) {
                                    if(panel.social.kind==realm::HudState::SocialKind::hall)game_session->use_portal(button.id);
                                    else if(interaction.open(panel.social,*player)) { hud_input.cancel();hud_mouse_down=false;tooltip_pointer_valid=false; }
                                }
                                else if(panel.sellable.id==button.id)game_session->buy(button.id);
                                // CharacterChangerPanel dispatches GameSprite.closed. The guild board,
                                // chronicle and reskin windows are not implemented.
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
                if(event.type==SDL_MOUSEBUTTONDOWN&&event.button.button==SDL_BUTTON_LEFT&&o.world&&minimap) {
                    int logical_width{},logical_height{};SDL_GetWindowSize(session.window,&logical_width,&logical_height);
                    if(logical_height>0) {
                        const double scale=logical_height/600.0,x=(event.button.x-logical_width)/scale+200,y=event.button.y/scale;
                        if(x>=176&&x<192) { if(y>=18&&y<34)minimap->zoom_out();else if(y>=8&&y<24)minimap->zoom_in(); }
                    }
                }
                if (event.type == SDL_KEYDOWN) {
                    const auto key = event.key.keysym.sym;
                    if (key == SDLK_ESCAPE) {
                        if (startup && game_session) { if (!event.key.repeat) { leaving_game = true; game_session->disconnect(); } }
                        else running = false;
                    }
                    if (key == SDLK_r && !event.key.repeat && game_session) game_session->escape();
                    if (key == SDLK_TAB && !startup) { o.world = !o.world;hud_input.cancel(); }
                    if(key==SDLK_b&&hud_art&&hud&&o.world&&!event.key.repeat)if(const auto* player=world.entity(world.player_id()))hud_input.cycle_tabs(realm::HudState::player(world,*player).inventory.backpack);
                    if(hud_art&&o.world&&!event.key.repeat&&key>=SDLK_1&&key<=SDLK_8&&game_session)if(const auto* player=world.entity(world.player_id()))game_session->use_item({player->id,static_cast<std::uint8_t>((hud_input.selected()==2?12:4)+key-SDLK_1)},time);
                    // MapUserInput handles every KEY_DOWN, including auto-repeat.
                    if(game_session&&(key==SDLK_f||key==SDLK_v))game_session->use_potion(key==SDLK_v,time);
                    // PortalPanel.onKeyDown: the default interact key enters even while Full or Locked.
                    if(game_session&&hud_art&&o.world&&key==SDLK_0)if(const auto* player=world.entity(world.player_id())) {
                        const auto state=realm::HudState::player(world,*player);
                        if(state.portal.id>=0)game_session->use_portal(state.portal.id);
                        else if(state.social.kind==realm::HudState::SocialKind::hall&&state.social.enabled)game_session->use_portal(state.social.id);
                    }
                    if (key == SDLK_c) centered = !centered;
                    if (key == SDLK_RIGHT) { selected = (selected + 1) % realm::flash::sheets.size(); page = 0; }
                    if (key == SDLK_LEFT) { selected = (selected + realm::flash::sheets.size() - 1) % realm::flash::sheets.size(); page = 0; }
                    if (key == SDLK_DOWN) ++page;
                    if (key == SDLK_UP) page = std::max(0, page - 1);
                    if (key == SDLK_EQUALS || key == SDLK_KP_PLUS) { if(o.world&&minimap)minimap->zoom_in();else zoom = std::min(4.0, zoom * 1.25); }
                    if (key == SDLK_MINUS || key == SDLK_KP_MINUS) { if(o.world&&minimap)minimap->zoom_out();else zoom = std::max(.25, zoom / 1.25); }
                }
            }
            if(auto request=interaction.take_request();request&&game_session&&game_session->state()==realm::SessionState::playing) {
                using K=realm::HudState::SocialKind;
                if(request->kind==K::create_guild)game_session->create_guild(request->value);
                else if(request->kind==K::leave_guild)game_session->leave_guild(request->value);
                else game_session->choose_name(request->value);
            }
            const auto keys = SDL_GetKeyboardState(nullptr);
            if (o.world && !o.benchmark && !menus && !interaction.visible()) {
                const double rotation = static_cast<double>(keys[SDL_SCANCODE_E]) - keys[SDL_SCANCODE_Q];
                const realm::Vec2 input{static_cast<double>(keys[SDL_SCANCODE_D])-keys[SDL_SCANCODE_A],static_cast<double>(keys[SDL_SCANCODE_S])-keys[SDL_SCANCODE_W]};
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
                int mouse_x{},mouse_y{};const auto buttons=SDL_GetMouseState(&mouse_x,&mouse_y);
                if (!menus && !interaction.visible() && ((buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) || keys[SDL_SCANCODE_SPACE])) {
                    int logical_w{},logical_h{},drawable_w{},drawable_h{};SDL_GetWindowSize(session.window,&logical_w,&logical_h);SDL_GL_GetDrawableSize(session.window,&drawable_w,&drawable_h);
                    if(logical_w>0&&logical_h>0&&drawable_w>drawable_h/3 && mouse_x>=0 && mouse_y>=0 && mouse_y<logical_h && static_cast<double>(mouse_x)*drawable_w/logical_w < drawable_w-drawable_h/3) {
                        camera.configure(position,angle,drawable_w,drawable_h,zoom,centered);
                        const auto target=camera.screen_to_world({static_cast<double>(mouse_x)*drawable_w/logical_w,static_cast<double>(mouse_y)*drawable_h/logical_h});
                        if(buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) game_session->shoot(std::atan2(target.y-position.y,target.x-position.x),time);
                        if(keys[SDL_SCANCODE_SPACE]) game_session->use_ability(target,time);
                    }
                }
                game_session->finish_frame(time);
            }
            int width{}, height{}; SDL_GL_GetDrawableSize(session.window, &width, &height);
            if (width <= 0 || height <= 0) { SDL_Delay(10); continue; }
            if (startup) startup->update(time, dt*1000, width, height);
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
                    scene.build(world, camera, atlas, animations, o.processed.empty() ? nullptr : &processed, terrain.get(), at, realm::Startup::background_angle, 1, width, height, time, quads);
                }
            } else if (o.world) {
                if (width <= height / 3) throw std::runtime_error("Window too narrow for game camera");
                camera.configure(position, angle, width, height, zoom, centered);
                clip_width = static_cast<int>(camera.playable_width());
                if (!o.map.empty() || game_session) {
                    scene.build(world, camera, atlas, animations, o.processed.empty() ? nullptr : &processed, terrain.get(), position, angle, zoom, clip_width, height, time, quads);
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
            std::array<realm::Quad,6> overlay_storage;std::span<const realm::Quad> overlay;
            if (startup) {
                for (const auto& area : startup->uploads()) atlas.update_additional_region(startup_index, startup->sheet(), area.x, area.y, area.width, area.height);
                startup->clear_uploads();
                menu_quads.clear(); startup->build(atlas, startup_index, time, menu_quads);
                if (!menu_quads.empty()) overlay = menu_quads;
            }
            if(hud&&o.world&&!menus)if(const auto* player=world.entity(world.player_id())) {
                auto state=realm::HudState::player(world,*player,game_session ? &game_session->party() : nullptr,time);
                const bool hover_enabled=tooltip_pointer_valid&&!hud_mouse_down&&!interaction.visible()&&game_session&&game_session->state()==realm::SessionState::playing;
                if(game_session)party_hover.update(world,game_session->party(),state,hud_view,hud_input.pointer(),hover_enabled,time);
                else party_hover.reset();
                if(player_tooltip) {
                    const auto target=hover_enabled ? realm::player_tooltip_target(world,party_hover.target()) : std::nullopt;
                    if(player_tooltip->update(target))atlas.update_additional(player_tooltip_index,player_tooltip->image());
                }
                if(!state.inventory.backpack&&hud_input.selected()==2)hud_input.cancel(true);
                if(state.inventory.container!=hud_container) { hud_input.container_changed();hud_container=state.inventory.container; }
                state.inventory.selected=hud_input.selected();state.inventory.dragging=hud_input.dragging();
                state.retain(hud_view);state.sellable.enabled=!game_session||!game_session->buy_pending();
                const auto panel_button=state.panel_button(fonts);
                const bool hovered=panel_button.id>=0&&realm::HudInput::button_at(panel_button.area,hud_input.pointer());
                state.portal.hover=hovered&&state.portal.id==panel_button.id;state.button.hover=hovered&&state.button.id==panel_button.id;
                state.sellable.hover=hovered&&state.sellable.id==panel_button.id;
                state.social.hover=hovered&&state.social.id==panel_button.id;
                if(hud->update(state))for(const auto& area:hud->changes())atlas.update_additional_region(hud_index,hud->image(),area.x,area.y,area.width,area.height);
                hud_view=state;
                if(tooltip) {
                    const auto target=tooltip_pointer_valid&&!hud_mouse_down&&!interaction.visible() ? realm::item_tooltip_target(world,state,hud_input.pointer()) : std::nullopt;
                    if(tooltip->update(target))atlas.update_additional(tooltip_index,tooltip->image());
                }
                const double ui_scale=height/600.0;
                auto q=realm::sprite_quad(atlas.additional(hud_index),{width-200*ui_scale,0},{200*ui_scale,0},{0,static_cast<double>(height)});q.kind=5;overlay_storage[0]=q;overlay=std::span<const realm::Quad>(overlay_storage).first(1);
                if(minimap) {
                    if(minimap->update(world,*player,angle))atlas.update_additional(minimap_index,minimap->image());
                    auto map_quad=realm::sprite_quad(atlas.additional(minimap_index),{width-196*ui_scale,4*ui_scale},{192*ui_scale,0},{0,192*ui_scale});map_quad.kind=5;overlay_storage[1]=map_quad;overlay=std::span<const realm::Quad>(overlay_storage).first(2);
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
                    auto region=atlas.additional(tooltip_index);region.height=tooltip->height();
                    auto tip=realm::sprite_quad(region,at,{realm::ItemTooltip::width*scale,0},{0,tooltip->height()*scale});tip.kind=5;
                    overlay_storage[overlay.size()]=tip;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                }
                if(player_tooltip&&player_tooltip->visible()) {
                    const auto pointer=hud_input.pointer();const realm::Vec2 screen_pointer{width+(pointer.x-200)*ui_scale,pointer.y*ui_scale};
                    const double scale=ui_scale;
                    const auto at=realm::PlayerTooltip::position(screen_pointer,width,height,player_tooltip->draw_width(),player_tooltip->height(),scale);
                    auto region=atlas.additional(player_tooltip_index);region.width=player_tooltip->draw_width();region.height=player_tooltip->height();
                    auto tip=realm::sprite_quad(region,at,{player_tooltip->draw_width()*scale,0},{0,player_tooltip->height()*scale});tip.kind=5;
                    overlay_storage[overlay.size()]=tip;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                }
            }
            if(interaction.visible()&&!menus) {
                if(interaction.render(fonts))atlas.update_additional(interaction_index,interaction.image());
                const auto at=realm::InteractionDialog::position(width,height);const double scale=realm::InteractionDialog::scale(width,height);
                auto dim=realm::Quad{};dim.origin={0,0};dim.axis_x={static_cast<float>(width),0};dim.axis_y={0,static_cast<float>(height)};dim.kind=2;dim.multiply={0,0,0,.55F};
                overlay_storage[overlay.size()]=dim;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
                auto frame_quad=realm::sprite_quad(atlas.additional(interaction_index),at,{realm::InteractionDialog::width*scale,0},{0,realm::InteractionDialog::height*scale});frame_quad.kind=5;
                overlay_storage[overlay.size()]=frame_quad;overlay=std::span<const realm::Quad>(overlay_storage).first(overlay.size()+1);
            }
            renderer.draw(atlas, quads, width, height, clip_width,overlay);
            const auto submitted = Clock::now();
            // Capture before swap; finite runs always capture their last rendered frame.
            if (!o.capture.empty() && (!o.frames ? frame == 0 : frame + 1 == static_cast<std::uint64_t>(o.frames)))
                realm::save_png(o.capture, renderer.capture(width, height));
            if (!script_capture.empty()) realm::save_png(script_capture, renderer.capture(width, height));
            SDL_GL_SwapWindow(session.window);
            const auto presented = Clock::now();
            const int warmup = o.frames > 60 ? 30 : 0;
            if (o.frames && frame >= static_cast<std::uint64_t>(warmup) && (o.capture.empty() || frame + 1 != static_cast<std::uint64_t>(o.frames))) {
                cpu_samples.push_back(milliseconds(submitted - start));
                present_samples.push_back(milliseconds(presented - submitted));
            }
            if (frame % 60 == 0 && !startup) {
                const std::string title = "Realm C++ harness | " + std::string(sheet.name) + (o.world ? (o.map.empty() ? " | camera fixture" : " | " + world.name()) : " | raw asset gallery") +
                    " | quads=" + std::to_string(quads.size()+overlay.size()) + " draws=" + std::to_string(renderer.draw_calls());
                SDL_SetWindowTitle(session.window, title.c_str());
            }
            if (o.frames && frame + 1 == static_cast<std::uint64_t>(o.frames))
                std::cout << "Last frame quads=" << quads.size()+overlay.size() << " draws=" << renderer.draw_calls() << " GPU_last_ms=" << renderer.gpu_ms() << '\n';
            if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL error during frame");
            ++frame;
        }
        std::cout << std::fixed << std::setprecision(4);
        report("CPU event/build/submit", std::move(cpu_samples)); report("Present", std::move(present_samples));
        return 0;
    } catch (const std::exception& error) { std::cerr << "realm_viewer: " << error.what() << '\n'; return 1; }
}
