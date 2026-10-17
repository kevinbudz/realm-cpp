#include "ApplicationState.hpp"
#include "ApplicationLoopDetail.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace realm::application {
namespace {
// Opens the window, then queries GL and prints the banner. Runs where the session sat in the old
// setup order.
realm::GraphicsInfo open_window(realm::platform::WindowSession& session, const Options& o,
                                const realm::Settings& settings) {
  session.open("Realm of the Mad God", o.width, o.height, o.hidden,
               o.benchmark              ? 0
               : settings.flag("vsync") ? 1
                                        : 0);
  const auto graphics = realm::graphics_info();
  std::cout << "Flash revision " << realm::flash::revision << "\nGPU " << graphics.renderer
            << "\nGL " << graphics.version << '\n';
  return graphics;
}

std::unique_ptr<realm::HudArt> make_hud_art(realm::Appearances& appearances,
                                            realm::Catalog& catalog) {
  auto art = std::make_unique<realm::HudArt>();
  if (art) {
    art->appearances(&appearances, &catalog);
  }
  return art;
}

// Startup and its background load run before the audio bridge, as before.
std::unique_ptr<realm::Startup> make_startup(const Options& o, realm::FontBook& fonts,
                                             realm::HudArt* hud_art, realm::Catalog& catalog,
                                             realm::World& world) {
  if (!o.client) {
    return nullptr;
  }
  auto startup = std::make_unique<realm::Startup>(
      fonts, hud_art, catalog, o.root,
      realm::StartupOptions{o.session.host, o.session.build_version, o.app_port, o.session.port,
                            o.account_file});
  startup->load_background(world);
  return startup;
}

std::unique_ptr<realm::StartupRml> make_startup_rml(const Options& o, realm::Startup* startup,
                                                    realm::ui::RetainedUi* production_ui) {
  if (!o.client) {
    return nullptr;
  }
  if (!production_ui) {
    throw std::runtime_error("The client UI needs fonts and HUD art");
  }
  return std::make_unique<realm::StartupRml>(*startup, production_ui->host());
}

// Offline fixture initialization runs first, then the stress timeline reads the world it set up.
std::unique_ptr<realm::quality::StressTimeline> make_offline_stress(realm::World& world,
                                                                    const realm::Catalog& catalog,
                                                                    const Options& o) {
  load_offline_map(world, catalog, o);
  if (o.stress.empty()) {
    return nullptr;
  }

  return std::make_unique<realm::quality::StressTimeline>(
      world, realm::quality::StressConfig::load(o.stress));
}

std::unique_ptr<realm::Terrain> make_terrain(
    realm::World& world, const Options& o,
    const std::unique_ptr<realm::quality::StressTimeline>& stress, std::size_t terrain_first,
    const std::shared_ptr<const realm::FlashImages>& flash_images) {
  if (!(!o.map.empty() || o.connect || o.client || stress)) {
    return nullptr;
  }
  auto terrain = std::make_unique<realm::Terrain>(
      world, flash_images, terrain_first, -1, o.connect || o.client || static_cast<bool>(stress));
  std::cout << "Terrain variants=" << terrain->images().size() << '\n';
  return terrain;
}

// The animation images come first, then the terrain's, as the terrain used to append them before
// the atlas was built.
realm::Atlas make_atlas(const realm::FlashImages& flash_images,
                        const realm::AnimationLibrary& animations, const realm::Terrain* terrain) {
  auto atlas_images = animations.images();
  if (terrain) {
    atlas_images.insert(atlas_images.end(), terrain->images().begin(), terrain->images().end());
  }
  return realm::Atlas(flash_images, atlas_images, animations.images().size(),
                      ClientState::dynamic_atlas_initial_pages);
}

realm::Image load_cursor_sheet(const std::filesystem::path& root) {
  const auto cursor_path = root / "src/kabam/rotmg/assets/EmbeddedAssets_cursorsEmbed_.png";
  return std::filesystem::exists(cursor_path) ? realm::load_png(cursor_path) : realm::Image{};
}

realm::platform::CursorSet make_cursor_set(const std::filesystem::path& root) {
  auto cursor_sheet = load_cursor_sheet(root);
  return realm::platform::CursorSet(std::move(cursor_sheet.pixels), cursor_sheet.width,
                                    cursor_sheet.height);
}

// The app host is copied before connect() moves o.session. The optional game session connects after
// the atlas.
std::string connect_session(std::unique_ptr<realm::Session>& game_session, realm::World& world,
                            Options& o) {
  const std::string app_host = o.session.host;
  if (o.connect) {
    if (o.password_stdin) {
      std::getline(std::cin, o.session.password);
    }
    game_session = std::make_unique<realm::Session>(world);
    game_session->connect(std::move(o.session));
  }
  return app_host;
}

// Validations run before the recorder file is created. Metadata that needs the renderer is added in
// the constructor.
std::unique_ptr<realm::telemetry::Recorder> make_telemetry(
    const Options& o, const std::unique_ptr<realm::quality::StressTimeline>& stress,
    const realm::GraphicsInfo& graphics, const realm::application::AudioBridge& audio_bridge) {
  if (!o.trace.empty() && o.telemetry.empty()) {
    throw std::invalid_argument("--trace requires --telemetry");
  }
  if (!o.trace.empty() && std::filesystem::absolute(o.trace).lexically_normal() ==
                              std::filesystem::absolute(o.telemetry).lexically_normal()) {
    throw std::invalid_argument("Trace and sample paths must differ");
  }

  std::unique_ptr<realm::telemetry::Recorder> telemetry;
  if (!o.telemetry.empty()) {
    telemetry = std::make_unique<realm::telemetry::Recorder>(o.telemetry, o.trace);
    if (stress) {
      telemetry->aggregate_repeated(true);
      telemetry->metadata(
          "zone_mode",
          "stress aggregate: cumulative inclusive duration per name anchored at first occurrence, "
          "not continuous spans");
      telemetry->metadata("manifest", o.stress.string());
    }
    telemetry->metadata("GL_vendor", graphics.vendor);
    telemetry->metadata("GL_renderer", graphics.renderer);
    telemetry->metadata("GL_version", graphics.version);
    telemetry->metadata("workload", !o.map.empty() ? o.map.string()
                                    : o.client     ? "client"
                                    : o.connect    ? "connect"
                                                   : "stress");
    telemetry->metadata("window", o.hidden ? "hidden" : "visible");
    telemetry->metadata("audio_mode", o.audio);
    telemetry->metadata("audio_output", audio_bridge.status());
  }
  return telemetry;
}
}  // namespace

ClientState::ClientState(Options options)
    : o(std::move(options)),
      // Parameters.data_: only the startup flow persists settings by default. The test-only modes
      // keep the default values unless a settings or account file is named, so fixtures never pick
      // up the player's bindings.
      persist_settings(o.client || !o.settings_file.empty() || !o.account_file.empty()),
      settings_path(!o.settings_file.empty() ? o.settings_file
                                             : realm::Settings::path_beside(o.account_file)),
      settings(persist_settings ? realm::Settings::load(settings_path) : realm::Settings{}),
      graphics(open_window(session, o, settings)),
      flash_images(std::make_shared<const realm::FlashImages>(o.root)),
      animations(*flash_images),
      catalog(o.root),
      appearances(flash_images, animations),
      fonts(o.root),

      hud_art(make_hud_art(appearances, catalog)),
      production_ui(hud_art && (o.client || o.world)
                        ? std::make_unique<realm::ui::RetainedUi>(fonts, hud_art.get())
                        : nullptr),
      hud(o.world ? std::make_unique<realm::Hud>(fonts, hud_art.get(), &catalog) : nullptr),
      minimap(hud ? std::make_unique<realm::Minimap>(catalog, animations, flash_images) : nullptr),

      drag_bitmap{128, 128, std::vector<unsigned char>(128 * 128 * 4)},
      tooltip(hud && hud_art ? std::make_unique<realm::ItemTooltip>(catalog, fonts, *hud_art)
                             : nullptr),
      player_tooltip(hud && hud_art
                         ? std::make_unique<realm::PlayerTooltip>(catalog, fonts, *hud_art)
                         : nullptr),
      player_menu(hud && hud_art && hud_art->contains("menuframe")
                      ? std::make_unique<realm::PlayerMenu>(catalog, fonts, *hud_art)
                      : nullptr),
      player_group(hud && hud_art ? std::make_unique<realm::PlayerGroup>(catalog, fonts, *hud_art)
                                  : nullptr),
      quest_tooltip(hud && hud_art ? std::make_unique<realm::QuestToolTip>(catalog, fonts, *hud_art)
                                   : nullptr),
      portrait_tooltip(hud && hud_art ? std::make_unique<realm::PortraitToolTip>(fonts, *hud_art)
                                      : nullptr),
      skins(hud && hud_art ? std::make_unique<realm::SkinChooser>(catalog, fonts, *hud_art)
                           : nullptr),
      chat_view(hud ? std::make_unique<realm::ChatView>(fonts, hud_art.get()) : nullptr),
      trade_view(hud && hud_art && hud_art->contains("tradefill0")
                     ? std::make_unique<realm::TradeView>(fonts, *hud_art)
                     : nullptr),
      guild_invite_view(hud ? std::make_unique<realm::GuildInviteView>(fonts) : nullptr),
      options_screen(production_ui ? std::make_unique<realm::OptionsScreen>(options_menu,
                                                                            production_ui->host())
                                   : nullptr),
      fame_popup(production_ui && hud_art ? std::make_unique<realm::FameContentPopup>(
                                               production_ui->host(), catalog, *hud_art)
                                         : nullptr),
      cursors(make_cursor_set(o.root)),
      members(hud_art ? *hud_art : members_art),

      world(catalog),
      tutorial(o.world ? std::make_unique<realm::Tutorial>(catalog, o.root) : nullptr),
      status_art(fonts, &appearances, true),
      startup(make_startup(o, fonts, hud_art.get(), catalog, world)),
      startup_rml(make_startup_rml(o, startup.get(), production_ui.get())),
      background_world(o.client),
      // One sound output for the process. Constructed before the session takes o.session (it reads
      // the app host).
      audio_bridge(o),

      stress(make_offline_stress(world, catalog, o)),
      terrain_first(animations.images().size()),
      terrain(make_terrain(world, o, stress, terrain_first, flash_images)),
      atlas(make_atlas(*flash_images, animations, terrain.get())),
      app_host(connect_session(game_session, world, o)),
      telemetry(make_telemetry(o, stress, graphics, audio_bridge)),

      renderer(base_capacity, true),
      position(o.map.empty() && !stress ? realm::Vec2{} : world.spawn_position()),
      angle(settings.camera_angle()),
      centered(settings.flag("centerOnPlayer")),
      applied_centering(settings.flag("centerOnPlayer")) {
  // Scene setters are in-memory only. No constructor above takes the scene, so running them after
  // the constructions (and after connect, which used to follow set_dynamic_atlas_pages) is not
  // observable.
  if (options_screen) {
    options_screen->set_cursors(load_cursor_sheet(o.root));
  }
  scene.set_cached_terrain(true);
  scene.set_status_art(&status_art);
  scene.set_dynamic_atlas_pages(dynamic_atlas_initial_pages, dynamic_atlas_max_pages, 120);

  if (telemetry) {
    telemetry->metadata("quad_stream",
                        renderer.persistent_streaming()
                            ? "persistent coherent ring above 64 KiB; small/busy streams orphan"
                            : "OpenGL 3.3 orphan stream");
  }

  std::cout << "Atlas pages=" << atlas.pages() << " bytes=" << atlas.bytes()
            << " PNGs=" << realm::flash::assets.size() << '\n';
  quads.reserve(base_capacity);
  if (o.frames) {
    cpu_samples.reserve(static_cast<std::size_t>(o.frames));
    present_samples.reserve(static_cast<std::size_t>(o.frames));
  }

  apply_settings();

  script = o.script.empty() ? std::vector<ScriptStep>{} : load_script(o.script);

  previous = Clock::now();
  epoch = previous;
}

std::string ClientState::own_name() {
  const auto* player = world.entity(world.player_id());
  return player ? player->profile.name : std::string{};
}

// Applies Parameters.data_ to the systems that read it: world flags, VSync, window mode and cursor.
void ClientState::apply_settings() {
  world.set_preferences(settings.preferences());
  audio_bridge.set_preferences(settings.preferences());
  // Performance Stats: the overlay starts from the saved showStatistics value.
  if (const bool stats = settings.flag("showStatistics"); stats != perf_stats) {
    perf_stats = stats;
    reset_perf();
  }
  if (const bool center = settings.flag("centerOnPlayer"); center != applied_centering) {
    applied_centering = center;
    centered = center;
  }
  if (o.benchmark) {
    return;
  }
  SDL_GL_SetSwapInterval(settings.flag("vsync") ? 1 : 0);
  const bool fullscreen = (SDL_GetWindowFlags(session.window) &
                           (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
  if (!o.hidden && settings.flag("fullscreenMode") != fullscreen) {
    SDL_SetWindowFullscreen(session.window,
                            settings.flag("fullscreenMode") ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  }
  if (!o.hidden && persist_settings) {
    cursors.select(settings.get("cursor"));
  }
}

void ClientState::save_settings() {
  settings.set_camera_angle(angle);
  if (persist_settings && !settings.save(settings_path)) {
    std::cerr << "Cannot write settings " << settings_path << '\n';
  }
}

// MapUserInput.clearInput, then the Options screen (the key, or CharacterDetailsView's button).
void ClientState::open_options() {
  hud_input.cancel();
  hud_mouse_down = false;
  tooltip_pointer_valid = false;
  // Options.onAddedToStage reads the window's real mode back into fullscreenMode.
  const bool fullscreen = (SDL_GetWindowFlags(session.window) &
                           (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
  if (settings.set_value("fullscreenMode", fullscreen ? 1 : 0)) {
    save_settings();
  }
  options_menu.open(settings);
}

void ClientState::reset_perf() {
  perf_frames = 0;
  perf_clock = 0;
  perf_history.clear();
  perf_intervals.clear();
  perf_previous_present.reset();
  perf_text.clear();
  perf_dirty = true;
}

void ClientState::sample_perf(Clock::time_point presented) {
  if (!perf_stats) {
    return;
  }
  // Measure completed swap intervals, including pacing and stalls;
  // simulation dt can be clamped or use a fixed fixture clock.
  if (perf_previous_present) {
    const double interval = detail::milliseconds(presented - *perf_previous_present);
    ++perf_frames;
    perf_clock += interval;
    perf_history.push_back({presented, interval});
    while (detail::milliseconds(presented - perf_history.front().presented) >= perf_history_ms) {
      perf_history.pop_front();
    }
  }
  perf_previous_present = presented;
  if (perf_clock >= perf_refresh_ms) {
    // Reuse the scratch storage and calculate tails only when the text refreshes.
    perf_intervals.clear();
    for (const auto& sample : perf_history) {
      perf_intervals.push_back(sample.interval);
    }
    // Nearest-rank percentiles; progressively partition just the remaining tail.
    std::size_t percentile_begin{};
    const auto percentile = [&](double fraction) {
      const auto index = static_cast<std::size_t>(
                             std::ceil(fraction * static_cast<double>(perf_intervals.size()))) -
                         1;
      std::nth_element(perf_intervals.begin() + percentile_begin, perf_intervals.begin() + index,
                       perf_intervals.end());
      percentile_begin = index;
      return perf_intervals[index];
    };
    const double p90 = percentile(.90);
    const double p99 = percentile(.99);
    std::ostringstream text;
    text << std::fixed << std::setprecision(1) << perf_frames * 1000 / perf_clock << " FPS";
    if (const double mb = realm::platform::resident_megabytes(); mb > 0) {
      text << '\n' << mb << " MB";
    }
    text << std::setprecision(3) << "\navg " << perf_clock / perf_frames << " ms\n"
         << "p90 " << p90 << " ms\np99 " << p99 << " ms";
    perf_text = text.str();
    perf_dirty = true;
    perf_frames = 0;
    perf_clock = 0;
  }
}

std::optional<realm::Quad> ClientState::perf_quad(int height) {
  if (!perf_stats) {
    return std::nullopt;
  }
  // Toggle Performance Stats: recent FPS, rolling frame-time percentiles and resources.
  if (perf_dirty && !perf_text.empty()) {
    const double ui = height / 600.0;
    constexpr int value_size = 12;
    constexpr double label_scale = 10.0 / value_size;
    constexpr unsigned outline_strength = 3;
    constexpr double column_gap = 6;
    constexpr double row_gap = 1;
    const double label_y = fonts.baseline(value_size, true) * (1 - label_scale);
    const auto rasterize_label = [&](const std::string& label_text) {
      // Render smaller labels directly from the native font at drawable density.
      fonts.set_pixel_scale(ui * label_scale);
      auto label = fonts.rasterize_glow(label_text, value_size, true, 0xd7dce2, outline_strength);
      fonts.set_pixel_scale(ui);
      label.width *= label_scale;
      label.height *= label_scale;
      label.offset.x *= label_scale;
      label.offset.y *= label_scale;
      label.image.pixel_scale = ui;
      label.redraw.reset();
      return label;
    };

    struct PerfRow {
      realm::TextBitmap label;
      realm::TextBitmap value;
      double height;
      bool regular;
    };

    std::vector<PerfRow> rows;
    double label_w = 0;
    double value_w = 0;
    double regular_w = 0;
    double box_h = 0;
    std::istringstream in(perf_text);
    for (std::string line; std::getline(in, line);) {
      const auto split = line.find(' ');
      if (split == std::string::npos) {
        continue;
      }
      const bool regular = line.ends_with(" FPS") || line.ends_with(" MB");
      auto label = regular ? realm::TextBitmap{} : rasterize_label(line.substr(0, split));
      auto value = fonts.rasterize_glow(regular ? line : line.substr(split + 1), value_size, true,
                                        0xffffff, outline_strength);
      const double row_h = regular ? value.height : std::max(label.height + label_y, value.height);
      if (regular) {
        regular_w = std::max(regular_w, value.width);
      } else {
        label_w = std::max(label_w, label.width);
        value_w = std::max(value_w, value.width);
      }
      box_h += row_h + row_gap;
      rows.push_back({std::move(label), std::move(value), row_h, regular});
    }
    // Light black outlines and smaller frame-time labels aligned to the value baseline.
    auto image =
        realm::ui_image(std::max(regular_w, label_w + column_gap + value_w) + 8, box_h + 8, ui);
    double row_y = 4;
    for (const auto& row : rows) {
      if (!row.regular) {
        realm::ui_blit_text(image, row.label, 4 + label_w - row.label.width + row.label.offset.x,
                            row_y + label_y + row.label.offset.y);
      }
      const double value_x = row.regular ? 4 : 4 + label_w + column_gap;
      realm::ui_blit_text(image, row.value, value_x + row.value.offset.x,
                          row_y + row.value.offset.y);
      row_y += row.height + row_gap;
    }
    perf_size = {static_cast<double>(image.width), static_cast<double>(image.height)};
    perf_texture.update(image);
    perf_dirty = false;
  }
  if (perf_text.empty()) {
    return std::nullopt;
  }
  return perf_texture.quad({4.0 * height / 600, 4.0 * height / 600}, {perf_size.x, 0},
                           {0, perf_size.y});
}

realm::ui::Viewport ClientState::window_viewport() {
  realm::ui::Viewport viewport;
  SDL_GetWindowSize(session.window, &viewport.window_width, &viewport.window_height);
  SDL_GL_GetDrawableSize(session.window, &viewport.drawable_width, &viewport.drawable_height);
  return viewport;
}

}  // namespace realm::application
