#include "StartupImpl.hpp"
#include "realm/appengine/SavedCharactersList.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace realm {
using namespace startup_detail;

void Startup::Impl::update(std::int32_t time, double dt, double width, double height) {
  if (state_.screen_ == Screen::fame && death_fade(state_.time_) > 0 && death_fade(time) == 0) view_model_.invalidate();
  const bool resized = view_model_.width() != width || view_model_.height() != height;
  if (resized) {
    view_model_.request_refresh();
    cancel_pointer();
    if (state_.delete_dialog_.visible()) view_model_.invalidate();
  }
  state_.time_ = time;
  view_model_.set_window(width, height);
  const bool scale_changed = view_model_.set_pixel_scale(fonts_.pixel_scale());
  if (scale_changed) view_model_.invalidate();
  if (state_.editor_) state_.editor_->resize(width, height, fonts_.pixel_scale());
  if (state_.screen_ == Screen::editor && (resized || scale_changed)) view_model_.invalidate();
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

}  // namespace realm
