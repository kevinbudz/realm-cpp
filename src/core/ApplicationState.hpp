#pragma once
#include "ApplicationAudio.hpp"
#include "ApplicationOptions.hpp"
#include "ApplicationScript.hpp"
#include "realm/assets/AnimationLibrary.hpp"
#include "realm/assets/Appearances.hpp"
#include "realm/stage3D/Atlas.hpp"
#include "realm/background/Backdrop.hpp"
#include "realm/map/Camera.hpp"
#include "realm/objects/Catalog.hpp"
#include "realm/ui/Chat.hpp"
#include "realm/objects/Combat.hpp"
#include "realm/hud/CreditDisplay.hpp"
#include "realm/ui/dialogs/Dialog.hpp"
#include "realm/stage3D/DrawTypes.hpp"
#include "realm/assets/FontBook.hpp"
#include "realm/ui/board/GuildBoard.hpp"
#include "realm/ui/guild/GuildMembers.hpp"
#include "realm/hud/Hud.hpp"
#include "realm/hud/HudInput.hpp"
#include "realm/ui/dialogs/InteractionDialog.hpp"
#include "realm/minimap/Minimap.hpp"
#include "realm/minimap/MinimapGpu.hpp"
#include "realm/objects/Movement.hpp"
#include "realm/game/IdleWatcher.hpp"
#include "realm/tutorial/Tutorial.hpp"
#include "realm/tutorial/TutorialView.hpp"
#include "realm/hud/KeysView.hpp"
#include "realm/ui/options/OptionsScreen.hpp"
#include "realm/map/partyoverlay/PartyArrows.hpp"
#include "realm/map/partyoverlay/QuestArrow.hpp"
#include "realm/ui/menu/PlayerGroup.hpp"
#include "realm/ui/menu/PlayerMenu.hpp"
#include "realm/ui/tooltip/PlayerTooltip.hpp"
#include "realm/ui/tooltip/PortraitToolTip.hpp"
#include "realm/ui/tooltip/QuestToolTip.hpp"
#include "realm/platform/Platform.hpp"
#include "realm/platform/PlatformSdl.hpp"
#include "realm/stage3D/Renderer.hpp"
#include "realm/hud/SafeAreaDisplay.hpp"
#include "realm/map/Scene.hpp"
#include "realm/messaging/Session.hpp"
#include "realm/parameters/Settings.hpp"
#include "realm/ui/SkinChooser.hpp"
#include "realm/screens/Startup.hpp"
#include "realm/screens/StartupRml.hpp"
#include "realm/fixtures/StressTimeline.hpp"
#include "realm/util/Telemetry.hpp"
#include "realm/map/Terrain.hpp"
#include "realm/ui/tooltip/TextToolTip.hpp"
#include "realm/ui/tooltip/ItemTooltip.hpp"
#include "realm/ui/Trade.hpp"
#include "realm/ui/panels/GuildInvite.hpp"
#include "realm/lib/ui/RetainedUi.hpp"
#include "realm/lib/ui/Stack.hpp"
#include "realm/map/World.hpp"
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

// The state one client run holds for its whole life: the locals the setup used to declare in run(),
// in the same order. Members are constructed in declaration order and destroyed in reverse, so GL
// objects are torn down before WindowSession and the session before the audio bridge. Where
// construction has side effects (window open, stdout, file and network I/O, stdin, validation
// throws), the member is initialized by a helper in ApplicationState.cpp that runs at that point of
// the declaration order.
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
  static constexpr int dynamic_atlas_initial_pages = 2;
  static constexpr int dynamic_atlas_max_pages = 12;
  static constexpr double perf_refresh_ms = 250;
  static constexpr double perf_history_ms = 1000;

  // Takes the parsed options. Startup side effects happen in the same order as the old run() setup,
  // then the body performs the trailing steps (quad_stream metadata, atlas print, apply_settings,
  // script load, loop clock).
  explicit ClientState(Options options);

  // Helpers the layers and the loop call. Bodies match the lambdas they replace.
  std::string own_name();
  void apply_settings();
  void save_settings();
  void open_options();
  void reset_perf();
  void sample_perf(Clock::time_point presented);
  std::optional<realm::Quad> perf_quad(int height);
  realm::ui::Viewport window_viewport();

  // A modal panel re-rasterizes at the new density when the window is resized under it.
  template <typename Redraw>
  void resized_panel(const UiEvent& e, bool visible, const Redraw& redraw) {
    if (e.kind == UiKind::resized && visible && e.viewport.drawable_height > 0) {
      fonts.set_pixel_scale(e.viewport.drawable_height / 600.0);
      redraw(e.viewport.drawable_width, e.viewport.drawable_height);
    }
  }

  // --- Options, settings, window/GL context, and shared asset libraries ---
  Options o;
  const bool persist_settings;
  const std::filesystem::path settings_path;
  realm::Settings settings;
  realm::platform::WindowSession session;
  const realm::GraphicsInfo graphics;
  // Every flash PNG, decoded once for the animation library, atlas, terrain, minimap, appearances.
  std::shared_ptr<const realm::FlashImages> flash_images;
  realm::AnimationLibrary animations;
  realm::Catalog catalog;
  realm::Appearances appearances;
  realm::FontBook fonts;

  // --- Persistent GPU surfaces and RmlUi host ---
  realm::SurfaceTexture hud_texture;
  realm::SurfaceTexture perf_texture;
  std::unique_ptr<realm::SurfaceTexture> death_background;
  realm::MinimapGpu minimap_gpu;
  std::unique_ptr<realm::HudArt> hud_art;
  std::unique_ptr<realm::ui::RetainedUi> production_ui;
  realm::ui::RmlStats previous_ui_stats;

  // --- HUD sidebar, readouts, drag icon, and hover tooltips/menus ---
  std::unique_ptr<realm::Hud> hud;
  std::unique_ptr<realm::Minimap> minimap;
  realm::CreditDisplayImage credit_image;
  realm::ui::RetainedSurface credit_texture;
  int displayed_credits = -1;
  int displayed_fame = -1;
  double credit_density{};
  realm::SafeAreaDisplayImage safe_area_image;
  realm::ui::RetainedSurface safe_area_texture;
  std::string displayed_guild;
  int displayed_stars = -2;
  int displayed_guild_rank = -1;
  double safe_area_density{};
  realm::Image drag_bitmap;
  realm::ui::RetainedSurface drag_surface;
  std::string drag_key;
  std::unique_ptr<realm::ItemTooltip> tooltip;
  realm::ui::RetainedSurface tooltip_texture;
  realm::PartyHover party_hover;
  std::unique_ptr<realm::PlayerTooltip> player_tooltip;
  realm::ui::RetainedSurface player_tooltip_texture;
  std::unique_ptr<realm::PlayerMenu> player_menu;
  realm::ui::RetainedSurface player_menu_texture;
  std::unique_ptr<realm::PlayerGroup> player_group;
  realm::ui::RetainedSurface group_texture;
  std::unique_ptr<realm::QuestToolTip> quest_tooltip;
  std::unique_ptr<realm::PortraitToolTip> portrait_tooltip;
  std::unique_ptr<realm::SkinChooser> skins;
  realm::ui::RetainedSurface skin_texture;
  bool skin_press_guard{};
  bool header_pressed{};
  std::string header_tip_key;
  realm::TextToolTipImage header_tip;
  realm::ui::RetainedSurface header_tip_texture;

  // --- Off-screen party and quest arrows ---
  realm::PartyArrows party_arrows;
  std::array<realm::SurfaceTexture, realm::Party::members_shown> arrow_textures;
  std::array<double, realm::Party::members_shown> arrow_rotations{};
  std::array<double, realm::Party::members_shown> arrow_densities{};
  std::array<bool, realm::Party::members_shown> arrow_hovers{};
  realm::QuestArrow quest_arrow;
  realm::SurfaceTexture quest_arrow_texture;
  realm::SurfaceTexture quest_tooltip_texture;
  double quest_arrow_rotation{};
  double quest_arrow_density{};
  realm::QuestArrow::Tooltip quest_tooltip_kind{};

  // --- Chat, speech balloons, trade, and guild invite ---
  realm::Chat chat;
  std::unique_ptr<realm::ChatView> chat_view;
  realm::ui::RetainedSurface chat_texture;
  realm::SpeechBalloons balloons;
  std::array<realm::SurfaceTexture, realm::SpeechBalloons::capacity> balloon_textures;
  std::array<std::uint64_t, realm::SpeechBalloons::capacity> balloon_uploaded{};
  realm::Trade trade;
  std::unique_ptr<realm::TradeView> trade_view;
  realm::ui::RetainedSurface trade_texture;
  realm::ui::RetainedSurface trade_request_texture;
  realm::GuildInvite guild_invite;
  std::unique_ptr<realm::GuildInviteView> guild_invite_view;
  realm::ui::RetainedSurface guild_invite_texture;
  int trade_pressed{-1};

  // --- Dungeon keys and modal screens ---
  realm::KeysView keys_view;
  realm::ui::RetainedSurface keys_texture;
  double keys_density{};
  std::uint64_t keys_revision{~0ULL};
  realm::OptionsScreen options_screen;
  realm::ui::RetainedSurface options_texture;
  realm::platform::CursorSet cursors;
  realm::InteractionDialog interaction;
  realm::ui::RetainedSurface interaction_texture;
  realm::Dialog dialog;
  realm::ui::RetainedSurface dialog_texture;
  realm::GuildBoard board;
  realm::ui::RetainedSurface board_texture;
  realm::HudArt members_art;
  realm::GuildMembers members;
  realm::ui::RetainedSurface members_texture;

  // --- HUD input and last-drawn snapshot ---
  bool tooltip_pointer_valid{};
  bool hud_mouse_down{};
  realm::HudInput hud_input;
  std::int32_t hud_container = -1;
  realm::HudState hud_view;
  realm::HudStateCache hud_state_cache;

  // --- World simulation, session, startup, audio, and renderer ---
  realm::World world;
  std::unique_ptr<realm::Tutorial> tutorial;
  realm::ui::RetainedSurface tutorial_texture;
  realm::Scene scene;
  realm::StatusArt status_art;
  realm::StatusTexts status_texts;
  realm::Backdrop backdrop;
  std::uint64_t backdrop_generation{~0ULL};
  std::unique_ptr<realm::Session> game_session;
  realm::IdleWatcher idle_watcher;
  std::unique_ptr<realm::Startup> startup;
  std::unique_ptr<realm::StartupRml> startup_rml;
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
  std::vector<double> cpu_samples;
  std::vector<double> present_samples;

  // --- Camera, player control, performance overlay, and script replay ---
  realm::Camera camera;
  realm::Vec2 position;
  double angle;
  double zoom = 1;
  bool centered;
  bool running = true;
  bool leaving_game{};
  bool applied_centering;
  bool autofire{};
  bool perf_stats{};
  int perf_frames{};
  double perf_clock{};
  std::string perf_text;
  bool perf_dirty{};
  realm::Vec2 perf_size{};
  std::deque<PerfSample> perf_history;
  std::vector<double> perf_intervals;
  std::optional<Clock::time_point> perf_previous_present;
  std::set<SDL_Scancode> script_keys;
  std::vector<ScriptStep> script;
  std::size_t script_next{};
  bool script_ability_down{};
  bool script_mouse_down{};
  realm::Vec2 script_pointer{};
  // MapUserInput.specialKeyDown_: one ability attempt per press, latched on success until the key
  // is released.
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
