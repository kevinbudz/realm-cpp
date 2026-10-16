#pragma once
#include "realm/assets/FontBook.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/messaging/Messages.hpp"
#include "realm/map/World.hpp"
#include "realm/map/mapoverlay/SpeechBalloons.hpp"
#include <deque>
#include <optional>
#include <vector>

namespace realm {
// Parameters chat names.
inline constexpr std::string_view server_chat_name = "";
inline constexpr std::string_view client_chat_name = "*Client*";
inline constexpr std::string_view error_chat_name = "*Error*";
inline constexpr std::string_view help_chat_name = "*Help*";
inline constexpr std::string_view guild_chat_name = "*Guild*";

struct ChatLine {
  std::int32_t time{};
  std::string name;
  int stars{-1};
  std::string recipient;
  bool to_me{};
  std::string text;
  // betterSkillys TEXT colors; 0 keeps the ElementFormats default.
  std::uint32_t name_color{};
  std::uint32_t text_color{};
  bool operator==(const ChatLine&) const = default;
};

// betterSkillys asks for the default player name color with this value; it draws as 0x00fe00.
inline constexpr std::uint32_t default_name_color = 0x123456;

// TextBoxLine.getTextBlock: the shown sender name and its colors.
struct ChatLineStyle {
  std::string name;
  std::uint32_t name_color{};
  std::uint32_t text_color{};
};

[[nodiscard]] ChatLineStyle chat_line_style(const ChatLine& line);

// TextBox.as: stored lines, the visible window, tell history and the input field.
// The native log keeps the newest `capacity` lines, far more than PageUp
// history normally reaches.
class Chat {
 public:
  static constexpr std::size_t capacity = 1000;
  static constexpr std::size_t max_lines = 10;
  static constexpr std::size_t line_scroll = 3;
  static constexpr std::size_t max_chars = 128;
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
  std::size_t current_teller_{};
  std::size_t scroll_{};
  bool show_max_{};
  bool typing_{};
  std::string input_;
  std::uint64_t revision_{};
};

// Renders the TextBox lines and input field into a bottom-anchored surface.
class ChatView {
 public:
  static constexpr int width = 600;
  static constexpr int height = 600;

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
    double x{};
    double width{};
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
  double scale_{};
  // Middle of a capital-and-digit row's ink, from the row top, at scale_.
  double text_center_{};
  double top_{};
  double surface_height_{};
  Image image_;
  std::uint64_t rebuilds_{};
};
}
