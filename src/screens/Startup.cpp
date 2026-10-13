#include "StartupImpl.hpp"
#include "realm/util/FlashDraw.hpp"
#include "realm/appengine/AppEngineClient.hpp"
#include "realm/map/Map.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <regex>
#include <stdexcept>
#include <utility>

namespace realm {
namespace {
using namespace startup_detail;
constexpr const char* legend_spans[]{"week", "month", "all"};
std::string read_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Cannot read " + path.string());
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}
void pop_utf8(std::string& value) {
  while (!value.empty()) {
    const auto c = static_cast<unsigned char>(value.back());
    value.pop_back();
    if ((c & 0xC0U) != 0x80U) break;
  }
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
void Startup::Impl::update(std::int32_t time, double dt, double width, double height) {
  if (state_.screen_ == Screen::fame && death_fade(state_.time_) > 0 && death_fade(time) == 0) view_model_.invalidate();
  if (view_model_.width() != width || view_model_.height() != height) {
    view_model_.request_refresh();
    cancel_pointer();
    if (state_.delete_dialog_.visible()) view_model_.invalidate();
  }
  state_.time_ = time;
  view_model_.set_window(width, height);
  if (view_model_.set_pixel_scale(fonts_.pixel_scale())) view_model_.invalidate();
  if (state_.background() && state_.background_width_ > 0) {
    state_.background_x_ += dt / 1000;
    if (state_.background_x_ > state_.background_width_ + 10) state_.background_x_ -= state_.background_width_;
  }
  if (auto completion = tasks_.poll()) finish_task(std::move(*completion));
  if (state_.retry_at_ >= 0 && time >= state_.retry_at_) {
    state_.retry_at_ = -1;
    if (state_.retry_task_ == Task::fame) {
      if (state_.screen_ == Screen::fame) request_fame();
    } else if (state_.retry_task_ == Task::legends) {
      if (state_.screen_ == Screen::legends) request_legends();
    } else if (state_.retry_task_ == Task::verify_saved)
      start_task(Task::verify_saved, "/account/verify",
                 {{"username", state_.saved_.username}, {"password", state_.saved_.password}});
    else
      start_task(Task::char_list, "/char/list", credentials());
  }
  if (state_.screen_ == Screen::fame && !state_.fame_ && !tasks_.busy() && state_.fame_error_.empty() &&
      state_.retry_at_ < 0)
    request_fame();
  if (state_.screen_ == Screen::fame && state_.fame_ && death_fade(time) == 0) {
    if (state_.score_at_ < 0) state_.score_at_ = time;
    const int frame =
        std::min((time - state_.score_at_) / 32, static_cast<int>(state_.fame_->lines.size()) * 500 / 32 + 20);
    if (!state_.fame_skip_ && frame != state_.fame_animation_frame_) {
      state_.fame_animation_frame_ = frame;
      view_model_.invalidate();
    }
    const double before = state_.fame_scroll_.pos();
    state_.fame_scroll_.tick(dt / 1000);
    if (before != state_.fame_scroll_.pos()) view_model_.invalidate();
  }
  if (state_.screen_ == Screen::characters && state_.dialog_ == Dialog::none && view_model_.list_height() > 400) {
    const double before = state_.character_scroll_.pos();
    state_.character_scroll_.tick(dt / 1000);
    if (before != state_.character_scroll_.pos()) {
      state_.scroll_ = state_.character_scroll_.pos() * (view_model_.list_height() - 400);
      view_model_.invalidate();
    }
  }
  if (state_.screen_ == Screen::legends) {
    // The last selected tab wins while one bounded HTTP request drains.
    if (!tasks_.busy() && !state_.legends_lists_[state_.legends_span_] && state_.legends_error_.empty() &&
        state_.retry_at_ < 0)
      request_legends();
    const auto before = state_.legends_scroll_.pos();
    state_.legends_scroll_.tick(dt / 1000);
    if (before != state_.legends_scroll_.pos()) view_model_.invalidate();
    state_.legends_positions_[state_.legends_span_] = state_.legends_scroll_.pos();
  }
  const bool caret = wants_text() && time / 500 % 2 == 0;
  if (caret != state_.caret_) state_.caret_ = caret;
  if (state_.skins_) {
    state_.skins_->poll();
    if (auto purchase = state_.skins_->take_purchase();
        purchase && purchase->username == state_.account_.username && state_.list_) {
      if (!purchase->error.empty()) {
        if (state_.screen_ == Screen::game)
          state_.skin_error_ = purchase->error;
        else if (state_.screen_ != Screen::skins)
          state_.skins_->purchase_error(purchase->error);
      } else if (std::find(state_.list_->owned_skins.begin(), state_.list_->owned_skins.end(), purchase->id) ==
                 state_.list_->owned_skins.end()) {
        state_.list_->owned_skins.push_back(purchase->id);
        state_.list_->credits = std::max(0, state_.list_->credits - 1000);
        view_model_.invalidate();
      }
    }
  }
  if (state_.screen_ == Screen::skins && state_.skins_) {
    state_.selected_skins_[state_.new_character_type_] = state_.skins_->selected();
    if (state_.skins_->update(width, height, time)) {
      const auto r = state_.skins_->change();
      if (r.width > 0 && r.height > 0) {
        view_model_.skin_changed(r);
      }
    }
    if (state_.list_) {
      if (state_.list_->credits != state_.skins_->data().credits) view_model_.invalidate();
      state_.list_->owned_skins = state_.skins_->data().owned_skins;
      state_.list_->credits = state_.skins_->data().credits;
    }
  }
  if (state_.skins_ && state_.skins_->visible() && state_.screen_ != Screen::skins && state_.skins_->modal_input())
    if (state_.skins_->update(width, height, time)) {
      const auto r = state_.skins_->change();
      if (r.width > 0 && r.height > 0) {
        view_model_.skin_changed(r);
      }
    }
  if (view_model_.dirty()) {
    view_model_.rebuild(state_, tasks_.busy());
    // A stationary pointer may now cover a different row after scrolling.
    if (state_.screen_ == Screen::characters && state_.dialog_ == Dialog::none && state_.character_pointer_valid_) {
      const int over = view_model_.hit(state_, state_.character_pointer_);
      if (over != state_.hovered_) {
        state_.hovered_ = over;
        view_model_.rebuild(state_, tasks_.busy());
      }
    }
  } else if (view_model_.pending())
    view_model_.refresh(state_, tasks_.busy());
}
void Startup::Impl::finish_task(startup_detail::HttpTaskService::Completion completion) {
  const auto task = completion.task;
  const auto& response = completion.response;
  const auto& error = completion.error;
  const auto& generations = completion.generations;
  const auto failure = error.empty() ? app_error(response) : std::optional<std::string>(error);
  view_model_.invalidate();
  switch (task) {
    case Task::delete_character:
      if (!state_.delete_selection_ || state_.delete_selection_->generation != state_.session_generation_ ||
          generations.session != state_.session_generation_ || state_.screen_ != Screen::characters ||
          state_.dialog_ != Dialog::deleting) {
        // A reply that no longer belongs to the visible dialog changes nothing, but the server may have deleted
        // the character, so any list fetched earlier is not authoritative.
        state_.invalidated_ = true;
        if (state_.screen_ == Screen::loading) start_task(Task::char_list, "/char/list", credentials());
        break;
      }
      if (error.empty() && failure) {
        std::cerr << "[startup] /char/delete attempt " << state_.delete_attempts_
                  << " failed: " << failure->substr(0, 256) << '\n';
        if (state_.delete_attempts_ < 3) {
          ++state_.delete_attempts_;
          send_delete();
          break;
        }
        show_delete_error(false);
        break;
      }
      try {
        if (!error.empty()) throw std::runtime_error(error);
        parse_delete_result(response);
      } catch (const std::exception& e) {
        // No usable answer: the server may or may not have deleted the character.
        std::cerr << "[startup] /char/delete reply unusable: " << std::string(e.what()).substr(0, 256) << '\n';
        show_delete_error(true);
        break;
      }
      {
        const int id = state_.delete_selection_->character_id;
        if (state_.list_) std::erase_if(state_.list_->characters, [id](const auto& c) { return c.id == id; });
        state_.account_.char_use.erase(id);
        try {
          save_account(options_.account_file, state_.account_);
        } catch (const std::exception&) {
        }
        cancel_delete();
        state_.invalidated_ = true;
        with_valid_data(Screen::characters);
      }
      break;
    case Task::fame:
      // A dismissal can happen before the HTTP worker finishes. Consume its
      // result without repopulating that screen, then run the queued refresh.
      if (state_.screen_ != Screen::fame || generations.fame != state_.fame_generation_) {
        if (state_.screen_ == Screen::loading) start_task(Task::char_list, "/char/list", credentials());
        break;
      }
      try {
        if (failure) throw std::runtime_error(*failure);
        state_.fame_ = parse_character_fame(response, state_.fame_character_id_);
        state_.fame_error_.clear();
        state_.score_at_ = -1;
        state_.fame_animation_frame_ = -1;
        state_.fame_scroll_.indicator(150, static_cast<double>(state_.fame_->lines.size()) * 28 + 15);
        state_.fame_scroll_.set_pos(1);
      } catch (const std::exception& e) {
        state_.fame_error_ = e.what();
        if (state_.fame_attempts_ < 3) {
          state_.retry_task_ = Task::fame;
          state_.retry_at_ = state_.time_ + 1000;
        }
      }
      break;
    case Task::legends:
      if (state_.screen_ == Screen::loading) {
        start_task(Task::char_list, "/char/list", credentials());
        break;
      }
      if (state_.screen_ != Screen::legends || generations.legends != state_.legends_generation_) break;
      try {
        if (failure) throw std::runtime_error(*failure);
        auto rows = parse_legends(response, legend_spans[generations.legends_span]);
        state_.legends_portraits_[generations.legends_span].resize(rows.size());
        state_.legends_lists_[generations.legends_span] = std::move(rows);
        if (generations.legends_span == state_.legends_span_) {
          state_.legends_error_.clear();
          state_.legends_scroll_.indicator(
              400, static_cast<double>(state_.legends_lists_[state_.legends_span_]->size()) * 56);
          state_.legends_scroll_.set_pos(state_.legends_positions_[state_.legends_span_]);
        }
      } catch (const std::exception& e) {
        if (generations.legends_span == state_.legends_span_) {
          state_.legends_error_ = e.what();
          if (state_.legends_attempts_ < 3) {
            state_.retry_task_ = Task::legends;
            state_.retry_at_ = state_.time_ + 1000;
          }
        }
      }
      break;
    case Task::verify_saved:
      // An unreachable server is retried; a rejected login continues as a guest.
      if (!error.empty()) {
        state_.loading_text_ = "Load error, retrying";
        state_.retry_task_ = Task::verify_saved;
        state_.retry_at_ = state_.time_ + 1000;
        break;
      }
      if (!failure) {
        state_.account_.username = state_.saved_.username;
        state_.account_.password = state_.saved_.password;
      }
      start_task(Task::char_list, "/char/list", credentials());
      break;
    case Task::char_list:
      if (generations.session != state_.session_generation_) {
        if (state_.screen_ == Screen::loading) start_task(Task::char_list, "/char/list", credentials());
        break;
      }
      if (failure) {
        state_.loading_text_ = "Load error, retrying";
        // GetCharListTask: rejected credentials are cleared and the guest list is requested at once.
        if (*failure == "Account credentials not valid") {
          state_.account_ = {};
          state_.selected_skins_.clear();
          if (state_.skins_) state_.skins_->close();
          try {
            save_account(options_.account_file, state_.account_);
          } catch (const std::exception&) {
          }
          start_task(Task::char_list, "/char/list", credentials());
        } else {
          state_.retry_task_ = Task::char_list;
          state_.retry_at_ = state_.time_ + 1000;
        }
        break;
      }
      try {
        state_.list_ = parse_char_list(response);
        sort_characters(*state_.list_, state_.account_.char_use);
        view_model_.reset_character_tooltip();
      } catch (const std::exception&) {
        state_.loading_text_ = "Load error, retrying";
        state_.retry_task_ = Task::char_list;
        state_.retry_at_ = state_.time_ + 1000;
        break;
      }
      state_.scroll_ = 0;
      state_.character_scroll_.set_pos(0);
      state_.list_loaded();
      if (state_.screen_ == Screen::skins && state_.skins_) {
        state_.skins_->endpoint(options_.host, options_.app_port, credentials());
        state_.skins_->open(state_.new_character_type_, state_.selected_skins_[state_.new_character_type_],
                            *state_.list_, true);
      }
      break;
    case Task::buy_slot:
      try {
        if (failure) throw std::runtime_error(*failure);
        parse_delete_result(response);
        state_.delete_dialog_.close();
        state_.dialog_ = Dialog::none;
        state_.invalidated_ = true;
        with_valid_data(Screen::characters);
      } catch (const std::exception&) {
        state_.dialog_ = Dialog::slot_error;
        state_.delete_dialog_.show(
            {"Unable to complete character slot purchase", std::nullopt, {"Close", std::nullopt}});
      }
      break;
    case Task::change_password:
      if (failure) {
        state_.fields_.at(0).error = *failure;
        break;
      }
      try {
        parse_delete_result(response);
      } catch (const std::exception& e) {
        state_.fields_.at(0).error = e.what();
        break;
      }
      state_.account_.password = state_.fields_.at(1).value;
      state_.saved_ = state_.account_;
      try {
        save_account(options_.account_file, state_.account_);
      } catch (const std::exception& e) {
        open_dialog(Dialog::message, "Password changed. Could not save it: " + std::string(e.what()));
        break;
      }
      open_dialog(Dialog::account_detail);
      break;
    case Task::login:
    case Task::reg:
      if (failure) {
        if (!state_.fields_.empty()) state_.fields_[task == Task::login ? 1 : 0].error = *failure;
        break;
      }
      state_.session_changed();
      state_.selected_skins_.clear();
      if (state_.skins_) state_.skins_->close();
      state_.account_.username = state_.fields_.at(0).value;
      state_.account_.password = state_.fields_.at(1).value;
      try {
        save_account(options_.account_file, state_.account_);
      } catch (const std::exception& e) {
        open_dialog(Dialog::message, e.what());
        break;
      }
      state_.dialog_ = Dialog::none;
      state_.invalidated_ = true;
      with_valid_data(state_.screen_ == Screen::characters || state_.screen_ == Screen::classes ||
                              state_.screen_ == Screen::skins
                          ? state_.screen_
                          : Screen::title);
      break;
    case Task::none:
      break;
  }
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
  state_.screen_ = Screen::entering;
  state_.loading_text_ = "Loading...";
  view_model_.invalidate();
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
  with_valid_data(Screen::characters);
  const auto notice =
      message + (message.empty() || state_.account_save_error_.empty() ? "" : "\n") + state_.account_save_error_;
  state_.account_save_error_.clear();
  if (!notice.empty()) open_dialog(Dialog::message, notice);
}
void Startup::Impl::died(const net::incoming::Death& death, std::int32_t time) {
  state_.session_changed();
  cancel_delete();
  if (state_.screen_ == Screen::fame) return;
  // FameView.setBackground plays death_screen as the death fade starts.
  state_.sounds_.play("death_screen");
  ++state_.fame_generation_;
  state_.fresh_fame_ = true;
  state_.fame_account_id_ = death.account_id;
  state_.fame_character_id_ = death.account_id == -1 ? -1 : death.character_id;
  state_.death_ = death;
  state_.death_at_ = time;
  state_.time_ = time;
  state_.fame_.reset();
  state_.fame_portrait_ = {};
  state_.fame_error_.clear();
  state_.fame_attempts_ = 0;
  state_.fame_skip_ = false;
  state_.score_at_ = -1;
  state_.fame_animation_frame_ = -1;
  state_.fame_scroll_.focus_lost();
  state_.retry_at_ = -1;
  state_.dialog_ = Dialog::none;
  state_.fields_.clear();
  state_.launch_.reset();
  if (state_.skins_) state_.skins_->close();
  state_.hovered_ = state_.pressed_ = -1;
  state_.invalidated_ = true;
  state_.screen_ = Screen::fame;
  view_model_.invalidate();
  request_fame();
}
void Startup::Impl::request_fame() {
  if (state_.screen_ != Screen::fame || tasks_.busy()) return;
  ++state_.fame_attempts_;
  state_.fame_error_.clear();
  view_model_.invalidate();
  start_task(
      Task::fame, "/char/fame",
      {{"accountId", std::to_string(state_.fame_account_id_)}, {"charId", std::to_string(state_.fame_character_id_)}});
}
double Startup::Impl::death_fade(std::int32_t time) const { return view_model_.death_fade(state_, time); }
Vec2 Startup::Impl::fame_point(Vec2 point) const {
  const double scale = view_model_.height() / 600;
  return {(point.x - (view_model_.width() - 800 * scale) / 2) / scale - 776, point.y / scale - 316};
}
void Startup::Impl::clear_legends() {
  ++state_.legends_generation_;
  state_.legends_scroll_.focus_lost();
  for (auto& rows : state_.legends_lists_) rows.reset();
  for (auto& icons : state_.legends_portraits_) std::vector<Image>().swap(icons);
  state_.legends_positions_ = {};
  state_.legends_error_.clear();
  state_.legends_attempts_ = 0;
}
void Startup::Impl::open_legends(bool from_death) {
  clear_legends();
  state_.legends_from_death_ = from_death;
  state_.legends_span_ = 0;
  state_.fresh_fame_ = false;
  ++state_.fame_generation_;
  state_.fame_.reset();
  state_.fame_portrait_ = {};
  state_.fame_error_.clear();
  state_.retry_at_ = -1;
  state_.screen_ = Screen::legends;
  state_.hovered_ = state_.pressed_ = -1;
  view_model_.invalidate();
  state_.legends_scroll_.indicator(400, 0);
  request_legends();
}
void Startup::Impl::request_legends() {
  if (state_.screen_ != Screen::legends || tasks_.busy()) return;
  ++state_.legends_attempts_;
  state_.legends_error_.clear();
  view_model_.invalidate();
  start_task(Task::legends, "/fame/list",
             {{"timespan", legend_spans[state_.legends_span_]},
              {"accountId", std::to_string(state_.list_ ? state_.list_->account_id : -1)},
              {"charId", std::to_string(state_.legends_from_death_ && state_.list_ && state_.list_->account_id != -1 &&
                                                state_.death_
                                            ? state_.death_->character_id
                                            : -1)}});
}
void Startup::Impl::show_legend(std::size_t index) {
  const auto& rows = state_.legends_lists_[state_.legends_span_];
  if (!rows || index >= rows->size()) return;
  state_.legends_positions_[state_.legends_span_] = state_.legends_scroll_.pos();
  state_.legends_scroll_.focus_lost();
  const auto& row = (*rows)[index];
  state_.fame_account_id_ = row.account_id;
  state_.fame_character_id_ = row.character_id;
  ++state_.fame_generation_;
  state_.fresh_fame_ = false;
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
}
Vec2 Startup::Impl::legends_point(Vec2 point) const {
  const double scale = view_model_.height() / 600;
  return {(point.x - (view_model_.width() - 800 * scale) / 2) / scale - 780, point.y / scale - 104};
}
void Startup::Impl::open_dialog(Dialog dialog, std::string message) {
  state_.dialog_ = dialog;
  state_.fields_.clear();
  state_.focus_ = 0;
  view_model_.invalidate();
  state_.hovered_ = state_.pressed_ = -1;
  if (dialog == Dialog::login) {
    state_.dialog_title_ = "Sign in";
    state_.fields_ = {{"Username", {}, {}, false}, {"Password", {}, {}, true}};
  } else if (dialog == Dialog::reg) {
    state_.dialog_title_ = "Register";
    state_.fields_ = {{"Username", {}, {}, false}, {"Password", {}, {}, true}, {"Retype Password", {}, {}, true}};
  } else if (dialog == Dialog::account_detail)
    state_.dialog_title_ = "Current account";
  else if (dialog == Dialog::change_password) {
    state_.dialog_title_ = "Change your password";
    state_.fields_ = {
        {"Password", {}, {}, true}, {"New Password", {}, {}, true}, {"Retype New Password", {}, {}, true}};
  } else {
    state_.dialog_title_ = "Realm C++";
    state_.dialog_message_ = std::move(message);
  }
}
void Startup::Impl::submit_dialog() {
  if (tasks_.busy()) return;
  if (state_.dialog_ == Dialog::message || state_.dialog_ == Dialog::account_detail) {
    state_.dialog_ = Dialog::none;
    view_model_.invalidate();
    return;
  }
  if (state_.dialog_ == Dialog::change_password) {
    for (auto& field : state_.fields_) field.error.clear();
    bool valid = true;
    if (state_.fields_[0].value.size() < 5) {
      state_.fields_[0].error = "Incorrect password";
      valid = false;
    } else if (state_.fields_[1].value.size() < 5) {
      state_.fields_[1].error = "New password too short";
      valid = false;
    } else if (state_.fields_[1].value != state_.fields_[2].value) {
      state_.fields_[2].error = "Password does not match";
      valid = false;
    }
    view_model_.invalidate();
    if (valid)
      start_task(Task::change_password, "/account/changePassword",
                 {{"username", state_.account_.username},
                  {"password", state_.fields_[0].value},
                  {"newPassword", state_.fields_[1].value}});
    return;
  }
  static const std::regex username(R"(^[a-zA-Z0-9]+$)");
  auto& name = state_.fields_.at(0);
  auto& password = state_.fields_.at(1);
  bool valid = true;
  if (!std::regex_match(name.value, username) || name.value.size() > 12) {
    name.error = "Invalid username";
    valid = false;
  }
  if (password.value.size() < 9) {
    password.error = "Invalid password";
    valid = false;
  }
  if (state_.dialog_ == Dialog::reg && state_.fields_.at(2).value != password.value) {
    state_.fields_.at(2).error = "The password did not match";
    valid = false;
  }
  view_model_.invalidate();
  if (!valid) return;
  if (state_.dialog_ == Dialog::login)
    start_task(Task::login, "/account/verify", {{"username", name.value}, {"password", password.value}});
  else
    start_task(Task::reg, "/account/register", {{"newUsername", name.value}, {"newPassword", password.value}});
}
void Startup::Impl::cancel_delete() {
  state_.delete_selection_.reset();
  state_.delete_dialog_.close();
  state_.dialog_ = Dialog::none;
  state_.delete_attempts_ = 0;
  state_.delete_refresh_ = false;
  state_.hovered_ = state_.pressed_ = -1;
  view_model_.invalidate();
}
void Startup::Impl::show_delete_error(bool refresh) {
  // Only this text is shown; the server's reason is logged by the caller.
  state_.dialog_ = Dialog::delete_error;
  state_.delete_refresh_ = refresh;
  state_.delete_selection_.reset();
  state_.delete_dialog_.show({"Unable to delete character", std::nullopt, {"Close", std::nullopt}});
  state_.hovered_ = state_.pressed_ = -1;
  view_model_.invalidate();
}
void Startup::Impl::close_delete_error() {
  const bool refresh =
      state_.delete_refresh_ && state_.dialog_ == Dialog::delete_error && state_.screen_ == Screen::characters;
  cancel_delete();
  if (refresh) {
    state_.invalidated_ = true;
    with_valid_data(Screen::characters);
  }
}
void Startup::Impl::confirm_delete(std::size_t index) {
  if (state_.screen_ != Screen::characters || state_.dialog_ != Dialog::none || tasks_.busy() || !state_.list_ ||
      index >= state_.list_->characters.size())
    return;
  const auto& c = state_.list_->characters[index];
  state_.delete_selection_ = DeleteSelection{c.id, state_.list_->account_id, credentials(), state_.session_generation_};
  const auto* def = c.object_type > 0 && c.object_type <= 65535
                        ? catalog_.object(static_cast<std::uint16_t>(c.object_type))
                        : nullptr;
  state_.dialog_ = Dialog::delete_confirm;
  state_.delete_dialog_.show({"Are you really sure you want to delete " + state_.list_->name.substr(0, 128) + " the " +
                                  (def ? def->display_id : "Unknown") + "?",
                              "Verify Deletion",
                              {"Cancel", "Delete"}});
  state_.hovered_ = state_.pressed_ = -1;
  view_model_.invalidate();
}
void Startup::Impl::submit_delete() {
  if (state_.dialog_ != Dialog::delete_confirm || tasks_.busy() || !state_.delete_selection_ || !state_.list_) return;
  const auto& selected = *state_.delete_selection_;
  if (selected.generation != state_.session_generation_ || selected.account_id != state_.list_->account_id ||
      selected.credentials != credentials() ||
      std::none_of(state_.list_->characters.begin(), state_.list_->characters.end(),
                   [&](const auto& c) { return c.id == selected.character_id; })) {
    cancel_delete();
    return;
  }
  state_.dialog_ = Dialog::deleting;
  state_.delete_dialog_.show({"Deleting Character...", std::nullopt, {std::nullopt, std::nullopt}});
  state_.delete_attempts_ = 1;
  send_delete();
  view_model_.invalidate();
  state_.hovered_ = state_.pressed_ = -1;
}
void Startup::Impl::send_delete() {
  if (!state_.delete_selection_) return;
  auto fields = state_.delete_selection_->credentials;
  fields.emplace_back("charId", std::to_string(state_.delete_selection_->character_id));
  start_task(Task::delete_character, "/char/delete", std::move(fields));
}
void Startup::Impl::activate(int action) {
  // Typed adapter actions must obey the same modal guard as native hit testing.
  if (state_.dialog_ != Dialog::none && action < dialog_actions) return;
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
        state_.screen_ = Screen::legends;
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
      open_dialog(Dialog::message, "The map editor is not available in the C++ client yet.");
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
    case StartupAction::Kind::none:
      break;
  }
  if (value >= 0) activate(value);
}
void Startup::Impl::set_field_value(std::size_t index, std::string value) {
  if (index >= state_.fields_.size() || tasks_.busy()) return;
  if (value.size() > 64) {
    value.resize(64);
    std::size_t lead = value.size() - 1;
    while (lead > 0 && (static_cast<unsigned char>(value[lead]) & 0xC0U) == 0x80U) --lead;
    const auto head = static_cast<unsigned char>(value[lead]);
    const std::size_t bytes = (head & 0xE0U) == 0xC0U   ? 2
                              : (head & 0xF0U) == 0xE0U ? 3
                              : (head & 0xF8U) == 0xF0U ? 4
                                                        : 1;
    if (lead + bytes > value.size()) value.resize(lead);
  }
  auto& field = state_.fields_[index];
  if (field.value == value) return;
  const bool clears_error = !field.error.empty();
  field.value = std::move(value);
  field.error.clear();
  if (clears_error)
    view_model_.invalidate();
  else {
    view_model_.sync_field(index, state_);
    view_model_.refresh(state_, tasks_.busy());
  }
}
std::string Startup::Impl::field_value(std::size_t index) const {
  return index < state_.fields_.size() ? state_.fields_[index].value : std::string{};
}
void Startup::Impl::pointer_move(Vec2 position) {
  state_.character_pointer_ = position;
  state_.character_pointer_valid_ = true;
  if (state_.screen_ == Screen::characters && state_.dialog_ == Dialog::none && view_model_.list_height() > 400) {
    const double scale = view_model_.height() / 600, offset = (view_model_.width() - 800 * scale) / 2;
    const double before = state_.character_scroll_.pos();
    if (state_.character_scroll_.move({(position.x - offset) / scale - 375, position.y / scale - 113}) ||
        before != state_.character_scroll_.pos()) {
      state_.scroll_ = state_.character_scroll_.pos() * (view_model_.list_height() - 400);
      view_model_.invalidate();
    }
  }
  if (state_.delete_dialog_.visible()) {
    state_.delete_dialog_.pointer_move(position);
    if (state_.delete_dialog_.update(fonts_, view_model_.width(), view_model_.height())) view_model_.invalidate();
  }
  if (state_.screen_ == Screen::legends && state_.dialog_ == Dialog::none) {
    const auto before = state_.legends_scroll_.pos();
    if (state_.legends_scroll_.move(legends_point(position)) || before != state_.legends_scroll_.pos())
      view_model_.invalidate();
  }
  if (state_.screen_ == Screen::fame && state_.fame_ && state_.dialog_ == Dialog::none) {
    const auto before = state_.fame_scroll_.pos();
    if (state_.fame_scroll_.move(fame_point(position)) || before != state_.fame_scroll_.pos()) view_model_.invalidate();
  }
  if (state_.skins_ && state_.skins_->visible() && state_.skins_->modal_input()) {
    state_.skins_->pointer_move(position);
    if (state_.hovered_ != -1) {
      state_.hovered_ = -1;
      view_model_.request_refresh();
      view_model_.refresh(state_, tasks_.busy());
    }
    return;
  }
  const int over = view_model_.hit(state_, position);
  if (over != state_.hovered_) {
    state_.hovered_ = over;
    view_model_.request_refresh();
    if (state_.screen_ == Screen::legends || state_.screen_ == Screen::characters) view_model_.invalidate();
  }
  if (state_.screen_ == Screen::skins && state_.dialog_ == Dialog::none && state_.skins_)
    state_.skins_->pointer_move(position);
  if (view_model_.has_character_tooltip()) {
    view_model_.position_character_tooltip(state_);
    view_model_.request_refresh();
  }
  if (view_model_.pending()) view_model_.refresh(state_, tasks_.busy());
}
void Startup::Impl::pointer_down(Vec2 position) {
  state_.character_pointer_ = position;
  state_.character_pointer_valid_ = true;
  if (state_.screen_ == Screen::characters && state_.dialog_ == Dialog::none && view_model_.list_height() > 400) {
    const double scale = view_model_.height() / 600, offset = (view_model_.width() - 800 * scale) / 2;
    const Vec2 local{(position.x - offset) / scale - 375, position.y / scale - 113};
    if (local.x >= 0 && local.x < 16 && local.y >= 0 && local.y < 399) {
      state_.character_scroll_.down(local);
      state_.scroll_ = state_.character_scroll_.pos() * (view_model_.list_height() - 400);
      view_model_.invalidate();
      state_.pressed_ = -1;
      return;
    }
  }
  if (state_.screen_ == Screen::legends && state_.dialog_ == Dialog::none &&
      state_.legends_lists_[state_.legends_span_] && state_.legends_lists_[state_.legends_span_]->size() * 56 > 400) {
    const auto local = legends_point(position);
    if (local.x >= 0 && local.x < 16 && local.y >= 0 && local.y < 400) {
      state_.legends_scroll_.down(local);
      view_model_.invalidate();
      state_.pressed_ = -1;
      return;
    }
  }
  if (state_.screen_ == Screen::fame && state_.fame_ && state_.dialog_ == Dialog::none &&
      death_fade(state_.time_) == 0) {
    const auto local = fame_point(position);
    if (local.x >= 0 && local.x < 16 && local.y >= 0 && local.y < 150) {
      state_.fame_skip_ = true;
      state_.fame_scroll_.down(local);
      view_model_.invalidate();
      state_.pressed_ = -1;
      return;
    }
  }
  if (state_.skins_ && state_.skins_->visible() && state_.skins_->modal_input()) {
    state_.skins_->pointer_down(position);
    state_.pressed_ = -1;
    return;
  }
  if (state_.screen_ == Screen::skins && state_.dialog_ == Dialog::none && state_.skins_) {
    state_.skins_->pointer_down(position);
    if (state_.skins_->modal_input()) {
      state_.pressed_ = -1;
      return;
    }
  }
  state_.pressed_ = view_model_.hit(state_, position);
  if (state_.screen_ == Screen::legends) view_model_.invalidate();
  // CreateNewCharacterRect and text fields respond to MOUSE_DOWN.
  if (state_.pressed_ == new_character_action || state_.pressed_ >= field_action) {
    activate(state_.pressed_);
    state_.pressed_ = -1;
  }
}
void Startup::Impl::pointer_up(Vec2 position) {
  state_.character_pointer_ = position;
  state_.character_pointer_valid_ = true;
  state_.character_scroll_.up();
  state_.legends_scroll_.up();
  state_.fame_scroll_.up();
  if (state_.skins_ && state_.skins_->visible() && state_.skins_->modal_input()) {
    state_.skins_->pointer_up(position);
    state_.pressed_ = -1;
    view_model_.request_refresh();
    return;
  }
  if (state_.screen_ == Screen::skins && state_.dialog_ == Dialog::none && state_.skins_) {
    const bool modal = state_.skins_->modal_input();
    state_.skins_->pointer_up(position);
    view_model_.request_refresh();
    if (modal) {
      state_.pressed_ = -1;
      return;
    }
  }
  const int target = view_model_.hit(state_, position);
  if (target >= 0 && target == state_.pressed_) {
    state_.sounds_.play("button_click");
    activate(target);
  }
  state_.pressed_ = -1;
  const int over = view_model_.hit(state_, position);
  if (over != state_.hovered_) {
    state_.hovered_ = over;
    view_model_.request_refresh();
    if (state_.screen_ == Screen::legends) view_model_.invalidate();
    view_model_.refresh(state_, tasks_.busy());
  }
}
void Startup::Impl::cancel_pointer() {
  state_.character_pointer_valid_ = false;
  if (state_.delete_dialog_.visible()) {
    state_.delete_dialog_.focus_lost();
    if (state_.delete_dialog_.update(fonts_, view_model_.width(), view_model_.height())) view_model_.invalidate();
  }
  if (state_.screen_ == Screen::legends || state_.screen_ == Screen::characters) view_model_.invalidate();
  state_.character_scroll_.focus_lost();
  state_.legends_scroll_.focus_lost();
  state_.fame_scroll_.focus_lost();
  state_.hovered_ = state_.pressed_ = -1;
  if (state_.skins_) state_.skins_->focus_lost();
  view_model_.request_refresh();
}
bool Startup::Impl::wheel(double steps) {
  if (state_.screen_ == Screen::legends && state_.dialog_ == Dialog::none) {
    state_.legends_scroll_.wheel(steps);
    view_model_.invalidate();
    return true;
  }
  if (state_.screen_ == Screen::fame && state_.dialog_ == Dialog::none) {
    if (state_.fame_ && death_fade(state_.time_) == 0) {
      state_.fame_skip_ = true;
      state_.fame_scroll_.wheel(steps);
      view_model_.invalidate();
    }
    return true;
  }
  if (state_.skins_ && state_.skins_->visible() && state_.skins_->modal_input()) return true;
  if (state_.screen_ == Screen::skins && state_.dialog_ == Dialog::none && state_.skins_) {
    state_.skins_->wheel(steps);
    return true;
  }
  if (state_.screen_ != Screen::characters || state_.dialog_ != Dialog::none || view_model_.list_height() <= 400)
    return false;
  state_.scroll_ = std::clamp(state_.scroll_ - steps * 30, 0.0, view_model_.list_height() - 400);
  state_.character_scroll_.set_pos(state_.scroll_ / (view_model_.list_height() - 400));
  view_model_.invalidate();
  return true;
}
void Startup::Impl::text(std::string_view utf8) {
  if (state_.skins_ && state_.skins_->visible() && state_.skins_->modal_input()) return;
  if (!wants_text() || state_.focus_ >= state_.fields_.size() || tasks_.busy()) return;
  auto& field = state_.fields_[state_.focus_];
  if (field.value.size() + utf8.size() > 64) return;
  field.value += utf8;
  field.error.clear();
  view_model_.invalidate();
}
bool Startup::Impl::key(StartupKey key) {
  if (!state_.menus()) return false;
  if (state_.skins_ && state_.skins_->visible() && state_.skins_->modal_input()) return true;
  if (state_.dialog_ == Dialog::none) {
    if ((state_.screen_ == Screen::fame || state_.screen_ == Screen::legends) &&
        (key == StartupKey::enter || key == StartupKey::escape)) {
      activate(back_action);
      return true;
    }
    return key != StartupKey::escape || state_.screen_ != Screen::title;
  }
  if (state_.dialog_ == Dialog::deleting || state_.dialog_ == Dialog::slot_buying) return true;
  if (state_.dialog_ == Dialog::slot_error) {
    if (key == StartupKey::enter || key == StartupKey::escape) activate(submit_action);
    return true;
  }
  if (tasks_.busy()) return true;
  if (state_.dialog_ == Dialog::delete_confirm || state_.dialog_ == Dialog::delete_error) {
    if (state_.dialog_ == Dialog::delete_error) {
      if (key == StartupKey::escape || key == StartupKey::enter) close_delete_error();
    } else if (key == StartupKey::escape)
      cancel_delete();
    return true;
  }
  view_model_.invalidate();
  switch (key) {
    case StartupKey::escape:
      if (!tasks_.busy()) activate(cancel_action);
      break;
    case StartupKey::enter:
      submit_dialog();
      break;
    case StartupKey::tab:
      if (!state_.fields_.empty()) state_.focus_ = (state_.focus_ + 1) % state_.fields_.size();
      break;
    case StartupKey::backspace:
      if (wants_text() && state_.focus_ < state_.fields_.size()) {
        pop_utf8(state_.fields_[state_.focus_].value);
        state_.fields_[state_.focus_].error.clear();
      }
      break;
  }
  return true;
}

bool Startup::Impl::wants_text() const {
  return state_.dialog_ == Dialog::login || state_.dialog_ == Dialog::reg || state_.dialog_ == Dialog::change_password;
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
void Startup::entered_game() { impl_->entered_game(); }
std::optional<std::string> Startup::take_skin_error() {
  return std::exchange(impl_->state_.skin_error_, std::optional<std::string>{});
}
const CharList* Startup::char_list() const { return impl_->state_.list_ ? &*impl_->state_.list_ : nullptr; }
const StartupViewSnapshot& Startup::view() const { return impl_->view_model_.snapshot(); }
void Startup::perform(StartupAction action) { impl_->perform(std::move(action)); }
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
