#include "StartupImpl.hpp"
#include <string>

namespace realm {
using namespace startup_detail;

void Startup::Impl::died(const net::incoming::Death& death, std::int32_t time) {
  if (state_.editor_test_) {
    left_game("the test character died");
    return;
  }
  state_.session_changed();
  cancel_delete();
  if (state_.screen_ == Screen::fame) return;
  // FameView.setBackground plays death_screen as the death fade starts.
  state_.sounds_.play("death_screen");
  ++state_.fame_generation_;
  state_.fresh_fame_ = true;
  state_.fame_from_news_ = false;
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
  state_.fame_from_news_ = false;
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
  state_.fame_from_news_ = false;
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

}  // namespace realm
