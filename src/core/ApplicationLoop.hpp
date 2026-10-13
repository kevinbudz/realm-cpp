#pragma once
#include "ApplicationLayers.hpp"
#include "ApplicationState.hpp"
#include "realm/util/Telemetry.hpp"
#include <SDL.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace realm::application {

// Per-frame values that cross the loop phases. run() makes one Frame per iteration. It is neither copied nor
// moved: overlay is a span into overlay_storage.
struct Frame {
  Frame() = default;
  Frame(const Frame&) = delete;
  Frame& operator=(const Frame&) = delete;
  Frame(Frame&&) = delete;
  Frame& operator=(Frame&&) = delete;

  std::chrono::steady_clock::time_point start{};
  double frame_interval{};
  double input_age{-60000};
  bool input_observed{};
  std::size_t previous_drops{};
  double dt{};
  std::int32_t time{};
  std::filesystem::path script_capture;
  bool menus{};
  int width{}, height{}, clip_width{};
  std::optional<realm::telemetry::Zone> cpu_zone, ui_zone;
  std::array<realm::Quad, 128> overlay_storage;
  std::span<const realm::Quad> overlay;
};

// SDL events in the stack's terms: pointer positions in drawable pixels, wheel in SDL steps (positive up).
std::optional<UiEvent> to_ui_event(const ClientState& cs, const SDL_Event& event);
void begin_frame(ClientState& cs, const Options& o, Frame& f);
void drive_stress_fixture(ClientState& cs, Frame& f);
void update_session(ClientState& cs, Frame& f);
void update_panels(ClientState& cs, Frame& f);
void sync_text_input(ClientState& cs);
void run_script_steps(ClientState& cs, Frame& f);
void update_panel_pixels(ClientState& cs, Frame& f);
void dispatch_events(ClientState& cs, const Options& o, Frame& f);
void after_dispatch(ClientState& cs);
void apply_input(ClientState& cs, const Options& o, Frame& f);
bool prepare_frame(ClientState& cs, Frame& f);
void build_scene(ClientState& cs, const Options& o, Frame& f);
void build_hud_overlays(ClientState& cs, const Options& o, Frame& f);
void build_group_overlays(ClientState& cs, const Options& o, Frame& f);
void build_modal_overlays(ClientState& cs, const Options& o, Frame& f);
void present_frame(ClientState& cs, const Options& o, Frame& f);
void shutdown_report(ClientState& cs, const Options& o);

}  // namespace realm::application
