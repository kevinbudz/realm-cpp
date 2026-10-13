#pragma once

#include "realm/screens/Startup.hpp"
#include "StartupHttpTasks.hpp"
#include "StartupState.hpp"
#include "StartupViewModel.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace realm::startup_detail {
inline constexpr const char* legend_spans[]{"week", "month", "all"};
}

namespace realm {
struct Startup::Impl {
  using Screen = startup_detail::Screen;
  using Dialog = startup_detail::Modal;
  using Task = startup_detail::Task;
  using DeleteSelection = startup_detail::StartupScreenState::DeleteSelection;

  Impl(const FontBook& fonts, const HudArt* art, const Catalog& catalog, const std::filesystem::path& root,
       StartupOptions options);
  ~Impl();

  void load_background(World& world);
  [[nodiscard]] bool wants_text() const;
  [[nodiscard]] net::FormFields credentials() const;
  void start_task(Task task, const std::string& path, net::FormFields fields);
  void update(std::int32_t time, double dt, double width, double height);
  void finish_task(startup_detail::HttpTaskService::Completion completion);
  void with_valid_data(Screen next);
  void play(const SavedCharacter* character, int class_type);
  void entered_game();
  void renamed_account(const std::string& name);
  void left_game(const std::string& message);
  void died(const net::incoming::Death& death, std::int32_t time);
  void request_fame();
  [[nodiscard]] double death_fade(std::int32_t time) const;
  [[nodiscard]] Vec2 fame_point(Vec2 point) const;
  void clear_legends();
  void open_legends(bool from_death);
  void request_legends();
  void show_legend(std::size_t index);
  [[nodiscard]] Vec2 legends_point(Vec2 point) const;
  void open_dialog(Dialog dialog, std::string message = {});
  void submit_dialog();
  void cancel_delete();
  void show_delete_error(bool refresh);
  void close_delete_error();
  void confirm_delete(std::size_t index);
  void submit_delete();
  void send_delete();
  void activate(int action);
  void perform(StartupAction action);
  void set_field_value(std::size_t index, std::string value);
  [[nodiscard]] std::string field_value(std::size_t index) const;
  void pointer_move(Vec2 position);
  void pointer_down(Vec2 position);
  void pointer_up(Vec2 position);
  void cancel_pointer();
  bool wheel(double steps);
  void text(std::string_view utf8);
  bool key(StartupKey key);

  const FontBook& fonts_;
  const HudArt* art_;
  const Catalog& catalog_;
  std::filesystem::path root_;
  StartupOptions options_;
  startup_detail::StartupScreenState state_;
  startup_detail::HttpTaskService tasks_;
  startup_detail::StartupViewModel view_model_;
};
}
