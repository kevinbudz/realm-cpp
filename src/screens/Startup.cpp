#include "StartupImpl.hpp"
#include "realm/util/FlashDraw.hpp"
#include "realm/net/Http.hpp"
#include "realm/appengine/SavedCharactersList.hpp"
#include "realm/map/World.hpp"
#include <chrono>
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <utility>

namespace realm {
namespace {
using namespace startup_detail;
std::string read_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Cannot read " + path.string());
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}  // namespace

Startup::Impl::Impl(const FontBook& fonts, const HudArt* art, const Catalog& catalog, const std::filesystem::path& root,
                    StartupOptions options)
    : fonts_(fonts),
      art_(art),
      catalog_(catalog),
      root_(root),
      options_(std::move(options)),
      view_model_(fonts, art, catalog, options_.build_version) {
  state_.editor_ = std::make_unique<EditorScreen>(catalog_, fonts_, root_, art_);
  if (options_.account_file.empty()) options_.account_file = account_path();
  auto logo = load_png(root_ / "src/kabam/rotmg/ui/view/TitleView_Logo.png");
  normalize_flash_png(logo);
  view_model_.set_logo(std::move(logo));
  state_.background_map_ =
      read_file(root_ / "src/kabam/rotmg/ui/view/components/MapBackground_EMBEDDED_BACKGROUNDMAP.dat");
  // ObjectLibrary.playerChars_: Player definitions in their XML order.
  for (const auto& object : catalog_.objects())
    if (object.class_name == "Player") state_.player_types_.push_back(object.type);
  if (state_.player_types_.empty()) throw std::runtime_error("No Player definitions in the object catalog");
  if (art_) state_.skins_ = std::make_unique<SkinChooser>(catalog_, fonts_, *art_);
  std::random_device random;
  state_.new_character_type_ = state_.player_types_[random() % state_.player_types_.size()];
  // WebLoadAccountTask: verify saved credentials, otherwise continue as a guest.
  state_.saved_ = load_account(options_.account_file);
  state_.account_.char_use = state_.saved_.char_use;
  if (!state_.saved_.username.empty())
    start_task(Task::verify_saved, "/account/verify",
               {{"username", state_.saved_.username}, {"password", state_.saved_.password}});
  else
    start_task(Task::char_list, "/char/list", credentials());
}
Startup::Impl::~Impl() { tasks_.wait(); }

void Startup::Impl::load_background(World& world) {
  constexpr int border = 10;
  world.load_jm_text(state_.background_map_, border);
  state_.background_width_ = world.width() - 2 * border;
  std::random_device random;
  state_.background_x_ = border;
  state_.background_y_ =
      border + static_cast<int>((world.height() - 2 * border) * std::uniform_real_distribution<double>(0, 1)(random));
}
net::FormFields Startup::Impl::credentials() const {
  return {{"username", state_.account_.username}, {"password", state_.account_.password}};
}
void Startup::Impl::start_task(Task task, const std::string& path, net::FormFields fields) {
  startup_detail::HttpTaskService::Generations generations{state_.session_generation_, state_.fame_generation_,
                                                           state_.legends_generation_, state_.legends_span_};
  tasks_.start(task, options_.host, options_.app_port, path, std::move(fields), generations);
}

void Startup::Impl::with_valid_data(Screen next) {
  view_model_.invalidate();
  if (!state_.require_valid_data(next)) return;
  // SetScreenWithValidDataCommand: LoadingScreen until GetCharListTask completes.
  start_task(Task::char_list, "/char/list", credentials());
}
void Startup::Impl::play(const SavedCharacter* character, int class_type) {
  // The server rejects HELLO without credentials, so a guest signs in first.
  if (!state_.account_.registered()) {
    open_dialog(Dialog::login);
    return;
  }
  if (!state_.list_) return;
  SessionOptions session;
  session.host = options_.host;
  session.port = options_.game_port;
  session.build_version = options_.build_version;
  session.username = state_.account_.username;
  session.password = state_.account_.password;
  session.game_id = -1;
  session.create_character = character == nullptr;
  session.character_id = character ? character->id : state_.list_->next_char_id;
  if (!character) {
    session.class_type = static_cast<std::uint16_t>(class_type);
    if (state_.screen_ == Screen::skins && state_.skins_ && state_.skins_->owned(state_.skins_->selected()))
      session.skin_type = static_cast<std::uint16_t>(state_.skins_->selected());
  }
  // PlayGameCommand.recordCharacterUseInSharedObject.
  state_.account_.char_use[session.character_id] = now_ms();
  try {
    save_account(options_.account_file, state_.account_);
  } catch (const std::exception&) {
  }
  state_.launch_ = std::move(session);
  state_.editor_test_ = false;
  state_.screen_ = Screen::entering;
  state_.loading_text_ = "Loading...";
  view_model_.invalidate();
}
void Startup::Impl::test_editor_map() {
  if (!state_.editor_) return;
  if (!state_.account_.registered()) {
    state_.editor_->set_status("Map testing requires a registered account. Log in, then return to the editor.");
    view_model_.invalidate();
    return;
  }
  if (!state_.list_) {
    state_.editor_->set_status("The character list is still loading. Try Test again in a moment.");
    view_model_.invalidate();
    return;
  }
  if (state_.list_->characters.empty()) {
    state_.editor_->set_status("Create a character before testing a map.");
    view_model_.invalidate();
    return;
  }
  try {
    auto map = state_.editor_->document().serialize();
    play(&state_.list_->characters.front(), 0);
    if (!state_.launch_) {
      state_.editor_->set_status("Could not start map test. Check the account and character list.");
      view_model_.invalidate();
      return;
    }
    state_.launch_->game_id = -4;
    state_.launch_->map_json = std::move(map);
    state_.editor_test_ = true;
  } catch (const std::exception& error) {
    state_.editor_->set_status(error.what());
    view_model_.invalidate();
  }
}
void Startup::Impl::entered_game() {
  state_.session_changed();
  cancel_delete();
  state_.enter_game();
  view_model_.invalidate();
}
void Startup::Impl::renamed_account(const std::string& name) {
  state_.session_changed();
  cancel_delete();
  state_.account_.username = name;
  state_.saved_.username = name;
  if (state_.list_) state_.list_->name = name;
  try {
    save_account(options_.account_file, state_.account_);
  } catch (const std::exception& e) {
    state_.account_save_error_ = "Your new login name is " + name + ". Could not save it: " + e.what();
  }
  state_.invalidated_ = true;
}
void Startup::Impl::left_game(const std::string& message) {
  const bool from_editor_test = state_.editor_test_;
  state_.session_changed();
  cancel_delete();
  clear_legends();
  state_.fresh_fame_ = false;
  state_.death_.reset();
  state_.fame_.reset();
  state_.fame_portrait_ = {};
  state_.fame_error_.clear();
  state_.retry_at_ = -1;
  state_.fame_scroll_.focus_lost();
  state_.dialog_ = Dialog::none;
  state_.fields_.clear();
  state_.launch_.reset();
  if (state_.skins_) state_.skins_->close();
  state_.invalidated_ = true;
  state_.hovered_ = state_.pressed_ = -1;
  if (from_editor_test) {
    state_.editor_test_ = false;
    state_.screen_ = Screen::editor;
    if (state_.editor_)
      state_.editor_->set_status(message.empty() ? "Map test ended. Edit the map or test it again."
                                                 : "Map test ended: " + message);
    view_model_.invalidate();
    return;
  }
  with_valid_data(Screen::characters);
  const auto notice =
      message + (message.empty() || state_.account_save_error_.empty() ? "" : "\n") + state_.account_save_error_;
  state_.account_save_error_.clear();
  if (!notice.empty()) open_dialog(Dialog::message, notice);
}

void Startup::Impl::activate(int action) {
  // Typed adapter actions must obey the same modal guard as native hit testing.
  if (state_.dialog_ != Dialog::none && action < dialog_actions) return;
  // SoundIcon.onIconClick: the icon works on every MenuBackground screen, so it comes before the screen branches.
  if (action == sound_action) {
    state_.play_sfx_ = !state_.play_sfx_;
    state_.play_sfx_request_ = state_.play_sfx_;
    view_model_.invalidate();
    return;
  }
  if (state_.dialog_ == Dialog::deleting || state_.dialog_ == Dialog::slot_buying ||
      (state_.dialog_ != Dialog::none && tasks_.busy()))
    return;
  if (state_.dialog_ == Dialog::slot_error) {
    if (action == cancel_action || action == submit_action) {
      state_.delete_dialog_.close();
      state_.dialog_ = Dialog::none;
      view_model_.invalidate();
    }
    return;
  }
  if (state_.dialog_ == Dialog::delete_confirm || state_.dialog_ == Dialog::delete_error) {
    if (action == cancel_action)
      state_.dialog_ == Dialog::delete_error ? close_delete_error() : cancel_delete();
    else if (action == submit_action) {
      if (state_.dialog_ == Dialog::delete_confirm)
        submit_delete();
      else
        close_delete_error();
    }
    return;
  }
  if (std::getenv("REALM_UI_ACTION_TRACE"))
    std::cerr << "[startup action] time=" << state_.time_ << " screen=" << static_cast<int>(state_.screen_)
              << " action=" << action << " size=" << view_model_.width() << 'x' << view_model_.height() << '\n';
  if (state_.screen_ == Screen::fame && state_.dialog_ == Dialog::none) {
    if (death_fade(state_.time_) > 0) return;
    if (action == back_action) {
      state_.retry_at_ = -1;
      if (state_.fresh_fame_)
        open_legends(true);
      else {
        state_.screen_ = state_.fame_from_news_ ? Screen::characters : Screen::legends;
        state_.fame_from_news_ = false;
        state_.fame_.reset();
        state_.fame_portrait_ = {};
        state_.fame_error_.clear();
        view_model_.invalidate();
      }
    } else if (action == play_action && !tasks_.busy()) {
      state_.fame_attempts_ = 0;
      state_.retry_at_ = -1;
      request_fame();
    }
    return;
  }
  if (state_.screen_ == Screen::legends && state_.dialog_ == Dialog::none) {
    if (action == back_action) {
      const bool from_death = state_.legends_from_death_;
      clear_legends();
      state_.retry_at_ = -1;
      state_.fresh_fame_ = false;
      if (from_death)
        left_game("");
      else {
        state_.screen_ = Screen::title;
        view_model_.invalidate();
      }
    } else if (action == play_action && !tasks_.busy()) {
      state_.legends_attempts_ = 0;
      state_.retry_at_ = -1;
      request_legends();
    } else if (action >= legend_tab_action && action < legend_tab_action + 3) {
      const auto next = static_cast<std::size_t>(action - legend_tab_action);
      if (next != state_.legends_span_) {
        state_.legends_positions_[state_.legends_span_] = state_.legends_scroll_.pos();
        state_.legends_scroll_.focus_lost();
        state_.legends_span_ = next;
        state_.hovered_ = state_.pressed_ = -1;
        state_.legends_error_.clear();
        state_.legends_attempts_ = 0;
        state_.retry_at_ = -1;
        view_model_.invalidate();
        const auto& rows = state_.legends_lists_[next];
        state_.legends_scroll_.indicator(400, rows ? static_cast<double>(rows->size()) * 56 : 0);
        state_.legends_scroll_.set_pos(state_.legends_positions_[next]);
        if (!rows) request_legends();
      }
    } else if (action >= legend_row_action && action < legend_row_action + 256)
      show_legend(static_cast<std::size_t>(action - legend_row_action));
    return;
  }
  if (state_.screen_ == Screen::characters && state_.dialog_ == Dialog::none && state_.list_ && action >= news_action &&
      action < news_action + 100) {
    const auto index = static_cast<std::size_t>(action - news_action);
    if (index >= state_.list_->news.size() || tasks_.busy()) return;
    const auto& link = state_.list_->news[index].link;
    if (link.starts_with("fame:")) {
      int character_id{};
      const auto value = std::string_view(link).substr(5);
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), character_id);
      if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || character_id <= 0) return;
      state_.fame_account_id_ = state_.list_->account_id;
      state_.fame_character_id_ = character_id;
      ++state_.fame_generation_;
      state_.fresh_fame_ = false;
      state_.fame_from_news_ = true;
      state_.fame_.reset();
      state_.fame_portrait_ = {};
      state_.fame_error_.clear();
      state_.fame_attempts_ = 0;
      state_.fame_skip_ = true;
      state_.score_at_ = -1;
      state_.fame_animation_frame_ = -1;
      state_.fame_scroll_.focus_lost();
      state_.retry_at_ = -1;
      state_.screen_ = Screen::fame;
      state_.hovered_ = state_.pressed_ = -1;
      view_model_.invalidate();
      request_fame();
    } else if (link.starts_with("http://") || link.starts_with("https://")) {
      state_.open_url_ = link;
    }
    return;
  }
  if (action >= field_action) {
    state_.focus_ = static_cast<std::size_t>(action - field_action);
    view_model_.refresh(state_, tasks_.busy());
    return;
  }
  view_model_.invalidate();
  switch (action) {
    case cancel_action:
      if (state_.dialog_ == Dialog::change_password)
        open_dialog(Dialog::account_detail);
      else
        state_.dialog_ = Dialog::none;
      return;
    case submit_action:
      submit_dialog();
      return;
    case link_action:
      open_dialog(state_.dialog_ == Dialog::account_detail ? Dialog::change_password
                  : state_.dialog_ == Dialog::login        ? Dialog::reg
                                                           : Dialog::login);
      return;
    case account_logout_action:
      state_.session_changed();
      state_.account_ = {};
      state_.saved_ = {};
      state_.selected_skins_.clear();
      if (state_.skins_) state_.skins_->close();
      try {
        save_account(options_.account_file, state_.account_);
      } catch (const std::exception&) {
      }
      state_.invalidated_ = true;
      open_dialog(Dialog::login);
      return;
    case play_action:
      if (state_.screen_ == Screen::skins) {
        if (state_.list_ && state_.list_->available_slots() > 0) play(nullptr, state_.new_character_type_);
        return;
      }
      if (state_.screen_ == Screen::title)
        with_valid_data(Screen::characters);
      else if (state_.list_ && !state_.list_->characters.empty())
        play(&state_.list_->characters.front(), 0);
      else
        state_.screen_ = Screen::classes;
      return;
    case account_action:
      if (state_.account_.registered())
        open_dialog(Dialog::account_detail);
      else
        open_dialog(Dialog::login);
      return;
    case legends_action:
      open_legends(false);
      return;
    case editor_action:
      state_.screen_ = Screen::editor;
      state_.dialog_ = Dialog::none;
      state_.hovered_ = state_.pressed_ = -1;
      if (state_.editor_) state_.editor_->set_status("Draw tiles, use the palette, then save or test your map.");
      view_model_.invalidate();
      return;
    case buy_slot_action:
      if (!state_.account_.registered()) {
        open_dialog(Dialog::login);
        return;
      }
      if (!state_.list_ || tasks_.busy()) return;
      if (state_.list_->fame < 2000) {
        state_.dialog_ = Dialog::slot_error;
        state_.delete_dialog_.show(
            {"Insufficient funds when trying to buy a slot.", "Not Enough Fame", {"Close", std::nullopt}});
      } else {
        state_.dialog_ = Dialog::slot_buying;
        state_.delete_dialog_.show({"Buying Character Slot...", std::nullopt, {std::nullopt, std::nullopt}});
        start_task(Task::buy_slot, "/account/purchaseCharSlot", credentials());
      }
      return;
    case login_action:
      if (!state_.account_.registered()) {
        open_dialog(Dialog::login);
        return;
      }
      // WebLogoutCommand: forget the account and reload the guest data.
      state_.session_changed();
      cancel_delete();
      state_.account_ = {};
      state_.selected_skins_.clear();
      if (state_.skins_) state_.skins_->close();
      try {
        save_account(options_.account_file, state_.account_);
      } catch (const std::exception&) {
      }
      state_.invalidated_ = true;
      with_valid_data(Screen::title);
      return;
    case register_action:
      open_dialog(Dialog::reg);
      return;
    case back_action:
      if (state_.screen_ == Screen::skins) {
        state_.screen_ = Screen::classes;
        if (state_.skins_) state_.skins_->close();
      } else
        state_.screen_ = state_.screen_ == Screen::classes ? Screen::characters : Screen::title;
      return;
    case classes_action:
      state_.screen_ = Screen::classes;
      return;
    case new_character_action:
      with_valid_data(Screen::classes);
      return;
    default:
      break;
  }
  if (state_.list_ && action >= delete_character_action &&
      action < delete_character_action + static_cast<int>(state_.list_->characters.size())) {
    confirm_delete(static_cast<std::size_t>(action - delete_character_action));
    return;
  }
  if (action >= class_action && action < class_action + static_cast<int>(state_.player_types_.size())) {
    state_.new_character_type_ = state_.player_types_[static_cast<std::size_t>(action - class_action)];
    if (state_.skins_ && state_.list_) {
      state_.screen_ = Screen::skins;
      state_.skins_->endpoint(options_.host, options_.app_port, credentials());
      state_.skins_->open(state_.new_character_type_, state_.selected_skins_[state_.new_character_type_], *state_.list_,
                          true);
    }
  } else if (state_.list_ && action >= character_action &&
             action < character_action + static_cast<int>(state_.list_->characters.size()))
    play(&state_.list_->characters[static_cast<std::size_t>(action - character_action)], 0);
}
void Startup::Impl::perform(StartupAction action) {
  int value = -1;
  switch (action.kind) {
    case StartupAction::Kind::play:
      value = play_action;
      break;
    case StartupAction::Kind::account:
      value = account_action;
      break;
    case StartupAction::Kind::legends:
      value = legends_action;
      break;
    case StartupAction::Kind::editor:
      value = editor_action;
      break;
    case StartupAction::Kind::login:
      value = login_action;
      break;
    case StartupAction::Kind::register_account:
      value = register_action;
      break;
    case StartupAction::Kind::back:
      value = back_action;
      break;
    case StartupAction::Kind::classes:
      value = classes_action;
      break;
    case StartupAction::Kind::new_character:
      value = new_character_action;
      break;
    case StartupAction::Kind::buy_slot:
      value = buy_slot_action;
      break;
    case StartupAction::Kind::character:
      if (state_.list_ && action.index < state_.list_->characters.size())
        value = character_action + static_cast<int>(action.index);
      break;
    case StartupAction::Kind::class_type:
      if (action.index < state_.player_types_.size()) value = class_action + static_cast<int>(action.index);
      break;
    case StartupAction::Kind::cancel:
      value = cancel_action;
      break;
    case StartupAction::Kind::submit:
      value = submit_action;
      break;
    case StartupAction::Kind::link:
      value = link_action;
      break;
    case StartupAction::Kind::field:
      if (action.index < state_.fields_.size()) value = field_action + static_cast<int>(action.index);
      break;
    case StartupAction::Kind::legend_tab:
      if (action.index < 3) value = legend_tab_action + static_cast<int>(action.index);
      break;
    case StartupAction::Kind::legend_row:
      if (action.index < 256) value = legend_row_action + static_cast<int>(action.index);
      break;
    case StartupAction::Kind::delete_character:
      if (state_.list_ && action.index < state_.list_->characters.size())
        value = delete_character_action + static_cast<int>(action.index);
      break;
    case StartupAction::Kind::account_logout:
      value = account_logout_action;
      break;
    case StartupAction::Kind::news:
      if (state_.list_ && action.index < std::min<std::size_t>(state_.list_->news.size(), 100))
        value = news_action + static_cast<int>(action.index);
      break;
    case StartupAction::Kind::sound:
      value = sound_action;
      break;
    case StartupAction::Kind::none:
      break;
  }
  if (value >= 0) activate(value);
}

Startup::Startup(const FontBook& fonts, const HudArt* art, const Catalog& catalog,
                 const std::filesystem::path& flash_root, StartupOptions options)
    : impl_(std::make_unique<Impl>(fonts, art, catalog, flash_root, std::move(options))) {}
Startup::~Startup() = default;

const Image& Startup::sheet() const { return impl_->view_model_.sheet(); }
void Startup::load_background(World& world) { impl_->load_background(world); }
bool Startup::menus() const { return impl_->state_.menus(); }
bool Startup::background() const { return impl_->state_.background(); }
Vec2 Startup::background_position() const { return {impl_->state_.background_x_, impl_->state_.background_y_}; }
void Startup::pointer_move(Vec2 position) { impl_->pointer_move(position); }
void Startup::pointer_down(Vec2 position) { impl_->pointer_down(position); }
void Startup::pointer_up(Vec2 position) { impl_->pointer_up(position); }
void Startup::cancel_pointer() { impl_->cancel_pointer(); }
bool Startup::wheel(double steps) { return impl_->wheel(steps); }
void Startup::text(std::string_view utf8) { impl_->text(utf8); }
bool Startup::key(StartupKey key) { return impl_->key(key); }
bool Startup::wants_text() const { return impl_->wants_text(); }
void Startup::update(std::int32_t time_ms, double dt_ms, double width, double height) {
  impl_->update(time_ms, dt_ms, width, height);
}
std::optional<SessionOptions> Startup::take_launch() { return std::exchange(impl_->state_.launch_, std::nullopt); }
void Startup::set_play_sfx(bool play_sfx) {
  if (impl_->state_.play_sfx_ == play_sfx) return;
  impl_->state_.play_sfx_ = play_sfx;
  impl_->view_model_.invalidate();
}
std::optional<bool> Startup::take_play_sfx() { return std::exchange(impl_->state_.play_sfx_request_, std::nullopt); }
void Startup::entered_game() { impl_->entered_game(); }
std::optional<std::string> Startup::take_skin_error() {
  return std::exchange(impl_->state_.skin_error_, std::optional<std::string>{});
}
const CharList* Startup::char_list() const { return impl_->state_.list_ ? &*impl_->state_.list_ : nullptr; }
const StartupViewSnapshot& Startup::view() const { return impl_->view_model_.snapshot(); }
void Startup::perform(StartupAction action) { impl_->perform(std::move(action)); }
bool Startup::editor_event(const EditorInput& event) { return impl_->editor_event(event); }
bool Startup::editor_covers(Vec2 point) const { return impl_->editor_covers(point); }
bool Startup::editor_wants_text() const {
  return impl_->state_.screen_ == startup_detail::Screen::editor && impl_->state_.editor_ &&
         impl_->state_.editor_->wants_text();
}
bool Startup::editor_captured() const { return impl_->editor_captured(); }
void Startup::set_field_value(std::size_t index, std::string value) { impl_->set_field_value(index, std::move(value)); }
std::string Startup::field_value(std::size_t index) const { return impl_->field_value(index); }
void Startup::acknowledge_sheet_upload(std::uint64_t revision) {
  impl_->view_model_.acknowledge_sheet_upload(revision);
}
void Startup::acknowledge_skin_upload(std::uint64_t revision) { impl_->view_model_.acknowledge_skin_upload(revision); }
void Startup::renamed_account(const std::string& name) { impl_->renamed_account(name); }
void Startup::left_game(const std::string& message) { impl_->left_game(message); }
void Startup::died(const net::incoming::Death& death, std::int32_t time_ms) { impl_->died(death, time_ms); }
SoundCues& Startup::sound_cues() { return impl_->state_.sounds_; }
void Startup::play_click() { impl_->state_.sounds_.play("button_click"); }
double Startup::death_fade(std::int32_t time_ms) const { return impl_->death_fade(time_ms); }

}  // namespace realm

namespace realm {
std::optional<std::string> Startup::take_open_url() { return std::exchange(impl_->state_.open_url_, std::nullopt); }
}
