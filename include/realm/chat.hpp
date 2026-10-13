#pragma once
#include "realm/font.hpp"
#include "realm/hud_art.hpp"
#include "realm/messages.hpp"
#include "realm/world.hpp"
#include <deque>
#include <optional>
#include <vector>

namespace realm {
// Parameters chat names.
inline constexpr std::string_view server_chat_name = "", client_chat_name = "*Client*", error_chat_name = "*Error*",
                                  help_chat_name = "*Help*", guild_chat_name = "*Guild*";

struct ChatLine {
  std::int32_t time{};
  std::string name;
  int stars{-1};
  std::string recipient;
  bool to_me{};
  std::string text;
  bool operator==(const ChatLine&) const = default;
};

// TextBox.as: stored lines, the visible window, tell history and the input field.
// The native log keeps the newest `capacity` lines, far more than PageUp
// history normally reaches.
class Chat {
 public:
  static constexpr std::size_t capacity = 1000, max_lines = 10, line_scroll = 3, max_chars = 128;
  static constexpr std::int32_t line_time = 20000;
  // TextBox.addText: client-generated lines without sender details.
  void add(std::string name, std::string text, std::int32_t time);
  // TextBox.addTextFull for a TEXT packet; own_name decides tell direction.
  void add(const net::incoming::Text& message, std::string_view own_name, std::int32_t time);
  [[nodiscard]] const std::deque<ChatLine>& lines() const { return lines_; }
  // refreshStatusBox: up to eleven lines; expired lines hide unless scrolled back.
  [[nodiscard]] std::vector<std::size_t> visible(std::int32_t time) const;
  // PageUp first reveals expired lines, then moves back three lines.
  void scroll_up();
  void scroll_down();
  [[nodiscard]] bool typing() const { return typing_; }
  [[nodiscard]] const std::string& input() const { return input_; }
  // selectInput, optionally replacing the field ("/" or "/g ").
  void open(std::optional<std::string_view> text = {});
  // insertTellPrefix: "/tell " plus a recent teller; Shift moves to older ones.
  void tell(bool backward);
  void text(std::string_view utf8);
  void backspace();
  // Enter sends non-empty text and always closes the field.
  [[nodiscard]] std::optional<std::string> submit();
  void close() {
    if (typing_) {
      typing_ = false;
      ++revision_;
    }
  }
  [[nodiscard]] std::uint64_t revision() const { return revision_; }

 private:
  void push(ChatLine line);
  std::deque<ChatLine> lines_;
  std::vector<std::string> tellers_;
  std::size_t current_teller_{}, scroll_{};
  bool show_max_{}, typing_{};
  std::string input_;
  std::uint64_t revision_{};
};

// Renders the TextBox lines and input field into a bottom-anchored surface.
class ChatView {
 public:
  static constexpr int width = 600, height = 600;
  ChatView(const FontBook& fonts, const HudArt* art) : fonts_(fonts), art_(art) {}
  // Rebuilds only when the visible lines, input or density changed.
  bool update(const Chat& chat, std::int32_t time);
  [[nodiscard]] const Image& image() const { return image_; }
  // Logical y of the surface's top edge within the 600x600 TextBox.
  [[nodiscard]] double top() const { return top_; }
  [[nodiscard]] double surface_height() const { return surface_height_; }
  [[nodiscard]] bool empty() const { return surface_height_ <= 0; }
  [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }

 private:
  struct Piece {
    std::string text;
    std::uint32_t color{};
    int icon{-1};
    double x{}, width{};
  };
  struct Row {
    std::vector<Piece> pieces;
    double height{};
  };
  [[nodiscard]] std::vector<Row> layout(const ChatLine& line) const;
  void glow();
  const FontBook& fonts_;
  const HudArt* art_{};
  std::vector<std::size_t> shown_;
  std::uint64_t revision_{~0ULL};
  double scale_{}, top_{}, surface_height_{};
  Image image_;
  std::uint64_t rebuilds_{};
};

// SpeechBalloon/MapOverlay: one balloon per speaking object above its sprite.
// Balloons live in map pixels; they scale with the world zoom, not the UI.
class SpeechBalloons {
 public:
  static constexpr std::size_t capacity = 32;
  struct Balloon {
    std::int32_t object_id{-1};
    // Starts on the first frame the balloon is drawn, as SpeechBalloon.draw.
    std::int32_t start{-1}, lifetime{};
    std::string text;
    std::uint32_t background{}, outline{}, color{};
    // Source texture height of the object's sprite, for the height offset.
    int texture_height{8};
    // GPU slot, and a revision bumped on every rasterization.
    std::size_t slot{};
    std::uint64_t revision{};
    Image image;
    // Text field size and the field origin inside the image, in map pixels.
    double field_width{}, field_height{}, full_height{};
  };
  // GameServerConnection.onText: objects in the map get a balloon colored by
  // sender kind; a new balloon replaces the object's previous one.
  void add(const World& world, const net::incoming::Text& message);
  void clear() { balloons_.clear(); }
  // Rasterizes new balloons at the font's current density and drops expired
  // ones or those whose object left the map.
  void update(const World& world, const FontBook& fonts, std::int32_t time);
  [[nodiscard]] const std::vector<Balloon>& balloons() const { return balloons_; }
  // Top-left of the image in screen pixels for an object's ground position
  // on screen, with the map scale (pixels per map pixel).
  [[nodiscard]] static Vec2 position(const Balloon& balloon, Vec2 object_screen, int object_size, double scale);
  static constexpr double margin = 17;

 private:
  void render(Balloon& balloon, const FontBook& fonts);
  std::vector<Balloon> balloons_;
  std::uint64_t revisions_{};
};
}
