#pragma once
#include "realm/hud/HudInput.hpp"
#include "realm/messaging/Messages.hpp"
#include <utility>

namespace realm {
// In-game account/guild frames. A request is emitted once per explicit submit;
// the server owns account balance, name, membership and rank changes.
class InteractionDialog {
 public:
  using Kind = HudState::SocialKind;

  struct Request {
    Kind kind;
    std::string value;
  };

  static constexpr int width = 440, height = 340;
  bool open(const HudState::Social& panel, const Entity& player);
  void reset();

  [[nodiscard]] bool visible() const { return kind_ != Kind::none; }

  [[nodiscard]] bool pending() const { return pending_; }

  [[nodiscard]] bool wants_text() const {
    return visible() && kind_ != Kind::leave_guild && !pending_;
  }

  // ChooseNameFrame: Flash has no Enter/Escape handling, only its buttons act.
  [[nodiscard]] bool name_frame() const {
    return kind_ == Kind::choose_name || kind_ == Kind::change_name;
  }

  [[nodiscard]] const std::string& value() const { return value_; }

  [[nodiscard]] const std::string& error() const { return error_; }

  void text(std::string_view input);
  void backspace();
  void cancel();
  void submit();
  void pointer_down(Vec2 point);
  void pointer_up(Vec2 point);

  void focus_lost() { pressed_ = -1; }

  void receive(const net::Event& event);
  void update(const World& world);

  [[nodiscard]] std::optional<Request> take_request() {
    return std::exchange(request_, std::nullopt);
  }

  [[nodiscard]] std::optional<std::string> take_name() {
    return std::exchange(accepted_name_, std::nullopt);
  }

  bool render(const FontBook& fonts);

  [[nodiscard]] const Image& image() const { return image_; }

  [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }

  // Centered in the window, shrinks only when the window cannot hold the frame.
  static double scale(double w, double h);
  static Vec2 position(double w, double h);
  static Vec2 local(Vec2 point, double w, double h);
  inline static constexpr HudRect cancel_button{20, 292, 150, 28};
  inline static constexpr HudRect submit_button{220, 292, 200, 28};

 private:
  [[nodiscard]] int hit(Vec2 point) const;
  void paint(const Image& source, int x, int y);
  void rectangle(int x, int y, int w, int h, std::uint32_t color);
  void draw_text(const FontBook& fonts, std::string_view value, int size, bool bold,
                 std::uint32_t color, int x, int y, int field = 0);
  void draw_name_frame(const FontBook& fonts);
  Kind kind_{Kind::none};
  std::int32_t player_id_{-1};
  std::string guild_;
  std::string value_;
  std::string error_;
  std::string title_;
  std::string note_;
  bool pending_{};
  bool dirty_{true};
  int pressed_{-1};
  std::optional<Request> request_;
  std::optional<std::string> accepted_name_;
  // Measured text buttons of the name frame, in canvas coordinates.
  HudRect name_cancel_{};
  HudRect name_submit_{};
  Image image_{width, height, std::vector<unsigned char>(width* height * 4)};
  std::uint64_t rebuilds_{};
};
}
