#pragma once
// Flash: kabam/rotmg/classes/view/CharacterSkinListView.as
#include "realm/appengine/SavedCharactersList.hpp"
#include "realm/objects/Catalog.hpp"
#include "realm/ui/dialogs/Dialog.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/net/Http.hpp"
#include <future>
#include <cmath>
#include <utility>
namespace realm {
// Source CharacterSkinListView is shared by the startup screen and reskin modal.
// Rows contain values; no entity or account pointers survive a frame.
class SkinChooser {
 public:
  struct Purchase {
    int id{};
    std::string username, error;
  };
  struct Row {
    int id{};
    std::string name;
    bool owned{};
  };
  SkinChooser(const Catalog& catalog, const FontBook& fonts, const HudArt& art)
      : catalog_(catalog), fonts_(fonts), art_(art) {}
  ~SkinChooser();
  void open(int class_type, int current, const CharList& data, bool startup = false);
  void fetch(int class_type, int current, std::string host, std::uint16_t port, net::FormFields credentials);
  void endpoint(std::string host, std::uint16_t port, net::FormFields credentials);
  void close();
  void poll();
  bool update(double width, double height, std::int32_t time = 0);
  void pointer_move(Vec2 point);
  void pointer_down(Vec2 point);
  void pointer_up(Vec2 point);
  void wheel(double steps);
  void focus_lost();
  void purchase_error(const std::string& message, bool insufficient = false);
  [[nodiscard]] bool visible() const { return visible_; }
  [[nodiscard]] bool startup() const { return startup_; }
  [[nodiscard]] bool modal_input() const { return error_.visible(); }
  [[nodiscard]] bool purchasing() const { return task_ == Task::buy; }
  [[nodiscard]] std::optional<Purchase> take_purchase() { return std::exchange(purchase_, {}); }
  [[nodiscard]] bool ready() const { return ready_; }
  [[nodiscard]] bool owned(int id) const;
  [[nodiscard]] int selected() const { return selected_; }
  [[nodiscard]] const std::vector<Row>& rows() const { return rows_; }
  [[nodiscard]] const CharList& data() const { return data_; }
  [[nodiscard]] std::optional<int> take_selection() { return std::exchange(selection_, {}); }
  [[nodiscard]] HudRegion change() const { return change_; }
  [[nodiscard]] const Image& image() const { return image_; }
  [[nodiscard]] double view_height() const { return view_height_; }
  [[nodiscard]] double scroll() const { return scroll_; }
  [[nodiscard]] Vec2 list_position() const { return {position_.x + 10 * scale_, position_.y + 37 * scale_}; }

 private:
  enum class Task { none, list, buy };
  void set_rows(int current);
  void request(Task task, std::string path, net::FormFields fields);
  void render();
  int hit(Vec2 point) const;
  Vec2 local(Vec2 point) const { return {(point.x - position_.x) / scale_, (point.y - position_.y) / scale_}; }
  double list_height() const { return rows_.empty() ? 0 : static_cast<double>(rows_.size()) * 65.0 - 5; }
  double bar_height() const { return std::floor(400 * 400 / list_height()); }
  bool scrolling() const { return list_height() > 400; }
  const Catalog& catalog_;
  const FontBook& fonts_;
  const HudArt& art_;
  CharList data_;
  std::vector<Row> rows_;
  int class_type_{}, selected_{}, requested_{}, hover_{-1}, pressed_{-1}, preview_{}, next_preview_{};
  std::int32_t time_{}, preview_at_{};
  int animation_frame_{-1};
  bool visible_{}, ready_{}, startup_{}, dirty_{true}, drag_{}, replace_modal_{}, preview_dirty_{};
  double width_{800}, height_{600}, scale_{1}, view_height_{}, scroll_{}, drag_offset_{};
  Vec2 position_{};
  Image image_;
  HudRegion change_{};
  Dialog error_;
  std::string host_, request_username_;
  std::uint16_t port_{};
  net::FormFields credentials_;
  Task task_{Task::none};
  std::future<std::string> request_;
  std::uint64_t generation_{}, request_generation_{};
  std::optional<int> selection_;
  std::optional<Purchase> purchase_;
};
}
