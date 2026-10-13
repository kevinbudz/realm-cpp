#include "StartupImpl.hpp"
#include <algorithm>
#include <regex>
#include <utility>

namespace realm {

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

bool Startup::Impl::wants_text() const {
  return state_.dialog_ == Dialog::login || state_.dialog_ == Dialog::reg || state_.dialog_ == Dialog::change_password;
}

}  // namespace realm
