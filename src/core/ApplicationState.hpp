#pragma once
#include "ApplicationAudio.hpp"
#include "ApplicationOptions.hpp"
#include "ApplicationScript.hpp"
#include "realm/util/AnimatedChars.hpp"
#include "realm/util/TextureRedrawer.hpp"
#include "realm/stage3D/SpriteAtlas.hpp"
#include "realm/background/Backdrop.hpp"
#include "realm/map/Camera.hpp"
#include "realm/objects/ObjectLibrary.hpp"
#include "realm/ui/Chat.hpp"
#include "realm/objects/Combat.hpp"
#include "realm/game/view/CreditDisplay.hpp"
#include "realm/ui/dialogs/Dialog.hpp"
#include "realm/stage3D/DrawTypes.hpp"
#include "realm/ui/fonts/FontBook.hpp"
#include "realm/ui/board/GuildBoardWindow.hpp"
#include "realm/ui/guild/GuildPlayerList.hpp"
#include "realm/hud/Hud.hpp"
#include "realm/hud/HudInput.hpp"
#include "realm/ui/dialogs/InteractionDialog.hpp"
#include "realm/minimap/MiniMap.hpp"
#include "realm/minimap/MiniMapGpu.hpp"
#include "realm/objects/Player.hpp"
#include "realm/ui/options/Options.hpp"
#include "realm/map/partyoverlay/PartyOverlay.hpp"
#include "realm/ui/menu/PlayerGroup.hpp"
#include "realm/ui/menu/PlayerMenu.hpp"
#include "realm/ui/tooltip/PlayerToolTip.hpp"
#include "realm/platform/Platform.hpp"
#include "realm/platform/PlatformSDL.hpp"
#include "realm/stage3D/Renderer.hpp"
#include "realm/game/view/SafeAreaDisplay.hpp"
#include "realm/map/Scene.hpp"
#include "realm/messaging/GameServerConnection.hpp"
#include "realm/parameters/Parameters.hpp"
#include "realm/classes/CharacterSkinListView.hpp"
#include "realm/screens/Startup.hpp"
#include "realm/screens/StartupRml.hpp"
#include "realm/fixtures/StressTimeline.hpp"
#include "realm/util/Telemetry.hpp"
#include "realm/map/Terrain.hpp"
#include "realm/ui/tooltip/TextToolTip.hpp"
#include "realm/ui/tooltip/EquipmentToolTip.hpp"
#include "realm/ui/TradePanel.hpp"
#include "realm/lib/ui/RetainedUI.hpp"
#include "realm/lib/ui/UIStack.hpp"
#include "realm/map/Map.hpp"
#include <SDL.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace realm::application {

// The state one client run holds for its whole life: the locals the setup used to declare in run(), in the same order.
// Members are constructed in declaration order and destroyed in reverse, so GL objects are torn down before
// WindowSession and the session before the audio bridge. Where construction has side effects (window open, stdout,
// file and network I/O, stdin, validation throws), the member is initialized by a helper in ApplicationState.cpp
// that runs at that point of the declaration order.
struct ClientState {
  using Clock = std::chrono::steady_clock;
  using UiEvent = realm::ui::Event;
  using UiKind = realm::ui::EventKind;
  struct PerfSample {
    Clock::time_point presented;
    double interval;
  };

  static constexpr std::size_t base_capacity = 4096;
  // The dynamic atlas layers are 2048x2048 RGBA (16 MiB each): start at 2 and grow on demand to 12.
  static constexpr int dynamic_atlas_initial_pages = 2, dynamic_atlas_max_pages = 12;
  static constexpr double perf_refresh_ms = 250, perf_history_ms = 1000;

  // Takes the parsed options. Startup side effects happen in the same order as the old run() setup, then the body
  // performs the trailing steps (quad_stream metadata, atlas print, apply_settings, script load, loop clock).
  explicit ClientState(Options options);

  // Helpers the layers and the loop call. Bodies match the lambdas they replace.
  std::string own_name();
  void apply_settings();
  void save_settings();
  void open_options();
  void reset_perf();
  realm::ui::Viewport window_viewport();

  // A modal panel re-rasterizes at the new density when the window is resized under it.
  template <typename Redraw>
  void resized_panel(const UiEvent& e, bool visible, const Redraw& redraw) {
    if (e.kind == UiKind::resized && visible && e.viewport.drawable_height > 0) {
      fonts.set_pixel_scale(e.viewport.drawable_height / 600.0);
      redraw(e.viewport.drawable_width, e.viewport.drawable_height);
    }
  }

  // The parsed options. Moved in; run() reads it through a reference.
  Options o;
  const bool persist_settings;
  const std::filesystem::path settings_path;
  realm::Settings settings;
  realm::platform::WindowSession session;
  const realm::GraphicsInfo graphics;
  realm::AnimationLibrary animations;
  realm::Catalog catalog;
  realm::Appearances appearances;
  realm::FontBook fonts;
  realm::SurfaceTexture hud_texture, perf_texture;
  std::unique_ptr<realm::SurfaceTexture> death_background;
  realm::MinimapGpu minimap_gpu;
  realm::ui::RetainedSurface tooltip_texture, player_tooltip_texture, player_menu_texture, chat_texture, trade_texture,
      trade_request_texture, interaction_texture, dialog_texture, board_texture, members_texture, group_texture,
      skin_texture, options_texture, header_tip_texture, drag_surface, credit_texture, safe_area_texture;
  realm::CreditDisplayImage credit_image;
  realm::SafeAreaDisplayImage safe_area_image;
  std::string displayed_guild;
  int displayed_stars = -2, displayed_guild_rank = -1;
  double safe_area_density{};
  int displayed_credits = -1, displayed_fame = -1;
  double credit_density{};
  std::unique_ptr<realm::HudArt> hud_art;
  std::unique_ptr<realm::ui::RetainedUi> production_ui;
  realm::ui::RmlStats previous_ui_stats;
  std::unique_ptr<realm::Hud> hud;
  std::unique_ptr<realm::Minimap> minimap;
  realm::Image drag_bitmap;
  std::unique_ptr<realm::ItemTooltip> tooltip;
  realm::PartyHover party_hover;
  std::unique_ptr<realm::PlayerTooltip> player_tooltip;
  std::unique_ptr<realm::PlayerMenu> player_menu;
  std::unique_ptr<realm::PlayerGroup> player_group;
  std::unique_ptr<realm::SkinChooser> skins;
  bool skin_press_guard{};
  bool header_pressed{};
  std::string header_tip_key;
  realm::TextToolTipImage header_tip;
  realm::PartyArrows party_arrows;
  std::array<realm::SurfaceTexture, realm::Party::members_shown> arrow_textures;
  std::array<double, realm::Party::members_shown> arrow_rotations{}, arrow_densities{};
  std::array<bool, realm::Party::members_shown> arrow_hovers{};
  // TextBox lines persist for the whole run, across maps and characters.
  realm::Chat chat;
  // The old setup built trade_view before chat_view; declaration order now builds chat_view first. Both constructors
  // are side-effect free, and destruction order is unchanged.
  std::unique_ptr<realm::ChatView> chat_view;
  realm::SpeechBalloons balloons;
  std::array<realm::SurfaceTexture, realm::SpeechBalloons::capacity> balloon_textures;
  std::array<std::uint64_t, realm::SpeechBalloons::capacity> balloon_uploaded{};
  realm::Trade trade;
  std::unique_ptr<realm::TradeView> trade_view;
  int trade_pressed{-1};
  realm::OptionsScreen options_screen;
  realm::platform::CursorSet cursors;
  realm::InteractionDialog interaction;
  realm::Dialog dialog;
  realm::GuildBoard board;
  realm::HudArt members_art;
  realm::GuildMembers members;
  bool tooltip_pointer_valid{}, hud_mouse_down{};
  realm::HudInput hud_input;
  std::string drag_key;
  std::int32_t hud_container = -1;
  // The interaction panel as last drawn; clicks hit what is on screen.
  realm::HudState hud_view;
  realm::HudStateCache hud_state_cache;
  realm::World world;
  realm::Scene scene;
  realm::StatusArt status_art;
  realm::StatusTexts status_texts;
  realm::Backdrop backdrop;
  std::uint64_t backdrop_generation{~0ULL};
  std::unique_ptr<realm::Session> game_session;
  std::unique_ptr<realm::Startup> startup;
  std::unique_ptr<realm::StartupRml> startup_rml;
  // World holds MapBackground's map until a session's MAPINFO replaces it.
  bool background_world{};
  realm::application::AudioBridge audio_bridge;
  std::unique_ptr<realm::quality::StressTimeline> stress;
  // Declared before terrain: the terrain's atlas images start at this index.
  const std::size_t terrain_first;
  std::unique_ptr<realm::Terrain> terrain;
  realm::Atlas atlas;
  const std::string app_host;
  std::unique_ptr<realm::telemetry::Recorder> telemetry;
  realm::Renderer renderer;
  std::vector<realm::Quad> quads;
  std::vector<double> cpu_samples, present_samples;
  realm::Camera camera;
  realm::Vec2 position;
  double angle, zoom = 1;
  bool centered, running = true, leaving_game{};
  bool applied_centering, autofire{}, perf_stats{};
  std::set<SDL_Scancode> script_keys;
  int perf_frames{};
  double perf_clock{};
  std::string perf_text;
  bool perf_dirty{};
  realm::Vec2 perf_size{};
  std::deque<PerfSample> perf_history;
  std::vector<double> perf_intervals;
  std::optional<Clock::time_point> perf_previous_present;
  std::vector<ScriptStep> script;
  std::size_t script_next{};
  bool script_ability_down{};
  bool script_mouse_down{};
  realm::Vec2 script_pointer{};
  // MapUserInput.specialKeyDown_: one ability attempt per press, latched on success until the key is released.
  realm::SpecialKeyLatch special_key;
  std::uint64_t frame{};
  Clock::time_point previous{};
  Clock::time_point epoch{};
  bool previous_capture{};
  realm::ui::Stack ui_stack;
  realm::ui::Viewport ui_viewport;
  std::int32_t ui_time{};
};

}
