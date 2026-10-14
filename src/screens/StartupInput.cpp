#include "StartupImpl.hpp"
#include <algorithm>

namespace realm {
namespace {
using namespace startup_detail;
void pop_utf8(std::string& value) {
  while (!value.empty()) {
    const auto c = static_cast<unsigned char>(value.back());
    value.pop_back();
    if ((c & 0xC0U) != 0x80U) break;
  }
}
}  // namespace

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
    if (state_.screen_ == Screen::legends || state_.screen_ == Screen::characters || state_.screen_ == Screen::classes)
      view_model_.invalidate();
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
    if (state_.screen_ == Screen::legends || state_.screen_ == Screen::classes) view_model_.invalidate();
    view_model_.refresh(state_, tasks_.busy());
  }
}
void Startup::Impl::cancel_pointer() {
  state_.character_pointer_valid_ = false;
  if (state_.delete_dialog_.visible()) {
    state_.delete_dialog_.focus_lost();
    if (state_.delete_dialog_.update(fonts_, view_model_.width(), view_model_.height())) view_model_.invalidate();
  }
  if (state_.screen_ == Screen::legends || state_.screen_ == Screen::characters || state_.screen_ == Screen::classes)
    view_model_.invalidate();
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

}  // namespace realm
