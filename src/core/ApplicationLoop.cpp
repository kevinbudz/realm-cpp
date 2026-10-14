#include "ApplicationLoop.hpp"
#include "ApplicationLoopDetail.hpp"
#include "realm/util/Telemetry.hpp"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "realm/platform/Platform.hpp"
#include "ApplicationOptions.hpp"
#include "ApplicationState.hpp"

namespace realm::application {
using detail::Clock;
using detail::milliseconds;
namespace {
void report(std::string_view name, std::vector<double> values) {
  if (values.empty()) return;
  std::sort(values.begin(), values.end());
  const double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
  const auto p95 = static_cast<std::size_t>(std::ceil(static_cast<double>(values.size()) * .95)) - 1;
  std::cout << name << " mean=" << mean << "ms p95=" << values[p95] << "ms samples=" << values.size() << '\n';
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
  cs.sample_perf(presented);
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
