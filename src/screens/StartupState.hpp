#pragma once

#include "realm/appengine/SavedCharactersList.hpp"
#include "realm/ui/dialogs/Dialog.hpp"
#include "realm/messaging/GameServerConnection.hpp"
#include "realm/classes/CharacterSkinListView.hpp"
#include "realm/ui/Scrollbar.hpp"
#include "realm/sound/SoundCues.hpp"
#include "StartupTypes.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace realm::startup_detail {
struct StartupScreenState {
  struct Field {
    std::string label, value, error;
    bool password{};
  };
  struct DeleteSelection {
    int character_id{}, account_id{};
    net::FormFields credentials;
    std::uint64_t generation{};
  };

  [[nodiscard]] bool menus() const { return screen_ != Screen::game; }
  [[nodiscard]] bool background() const { return menus() && screen_ != Screen::account_loading; }

  // These transitions keep the screen and its data-loading destination in sync.
  void session_changed() { ++session_generation_; }
  [[nodiscard]] bool require_valid_data(Screen next) {
    if (!invalidated_) {
      screen_ = next;
      return false;
    }
    pending_screen_ = next;
    screen_ = Screen::loading;
    loading_text_ = "Loading...";
    return true;
  }
  void list_loaded() {
    invalidated_ = false;
    loading_text_ = "Loading...";
    if (screen_ == Screen::account_loading || screen_ == Screen::loading) screen_ = pending_screen_;
  }
  void enter_game() {
    screen_ = Screen::game;
    dialog_ = Modal::none;
    hovered_ = pressed_ = -1;
  }

  SavedAccount saved_, account_;
  std::string account_save_error_;
  std::optional<CharList> list_;
  bool invalidated_{true};
  Screen screen_{Screen::account_loading}, pending_screen_{Screen::title};
  Modal dialog_{Modal::none};
  SoundCues sounds_;
  std::string loading_text_{"Loading..."}, dialog_message_, dialog_title_;
  std::vector<Field> fields_;
  std::size_t focus_{};
  Task retry_task_{Task::char_list};
  std::optional<DeleteSelection> delete_selection_;
  // DeleteCharacterTask.setMaxRetries(2): three attempts, retried only after an explicit <Error>.
  int delete_attempts_{};
  // An ambiguous reply may have deleted the character; the list is refreshed once the error is dismissed.
  bool delete_refresh_{};
  realm::Dialog delete_dialog_;
  std::uint64_t session_generation_{};

  std::int32_t retry_at_{-1}, time_{};
  std::optional<net::incoming::Death> death_;
  std::optional<CharacterFame> fame_;
  Image fame_portrait_;
  std::string fame_error_;
  std::int32_t death_at_{}, score_at_{-1};
  int fame_animation_frame_{-1};
  int fame_attempts_{};
  bool fame_skip_{};
  bool caret_{};
  Scrollbar fame_scroll_{16, 150};
  int fame_account_id_{}, fame_character_id_{};
  std::uint64_t fame_generation_{};
  bool fresh_fame_{}, legends_from_death_{};
  // One bounded list per tab; generations distinguish close/reopen while a request drains.
  std::uint64_t legends_generation_{};
  std::size_t legends_span_{};
  std::array<std::optional<std::vector<Legend>>, 3> legends_lists_;
  std::array<std::vector<Image>, 3> legends_portraits_;
  std::array<double, 3> legends_positions_{};
  std::string legends_error_;
  int legends_attempts_{};
  Scrollbar legends_scroll_{16, 400};
  std::vector<int> player_types_;
  int new_character_type_{};
  std::map<int, int> selected_skins_;
  std::optional<std::string> skin_error_;
  Vec2 character_pointer_{};
  bool character_pointer_valid_{};
  Scrollbar character_scroll_{16, 399};
  double scroll_{};
  std::optional<SessionOptions> launch_;
  std::string background_map_;
  double background_x_{10}, background_y_{10}, background_width_{};
  int hovered_{-1}, pressed_{-1};
  std::unique_ptr<SkinChooser> skins_;
};
}
