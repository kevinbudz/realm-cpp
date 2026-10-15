#pragma once

#include "realm/mapeditor/MapDocument.hpp"
#include "realm/assets/FontBook.hpp"
#include "realm/assets/Image.hpp"
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace realm {
class Catalog;
class HudArt;

enum class EditorTool { draw, erase, sample, name };
enum class EditorInputKey {
  none,
  escape,
  enter,
  backspace,
  tab,
  shift,
  control,
  left,
  right,
  up,
  down,
  undo,
  redo,
  draw,
  erase,
  sample,
  name,
  clear,
  load,
  save,
  test,
  back,
  zoom_in,
  zoom_out
};

struct EditorInput {
  enum class Kind {
    pointer_move,
    pointer_down,
    pointer_up,
    wheel,
    key_down,
    key_up,
    text,
    pointer_left,
    focus_lost
  };
  Kind kind{};
  Vec2 point{};  // Drawable pixels, in the same coordinates as the startup view.
  int button{1};
  double wheel{};
  EditorInputKey key{EditorInputKey::none};
  std::string text;
  bool shift{}, control{}, repeat{};
};

// Headless Flash map-editor screen and input state. The bitmap is rebuilt only when the document or
// view changes.
class EditorScreen {
 public:
  EditorScreen(const Catalog& catalog, const FontBook& fonts,
               const std::filesystem::path& flash_root, const HudArt* art = nullptr);

  void resize(double drawable_width, double drawable_height, double pixel_scale);
  void handle(const EditorInput& event);
  void cancel_pointer();
  [[nodiscard]] bool covers(Vec2 drawable_point) const;
  [[nodiscard]] bool wants_text() const;

  [[nodiscard]] bool captured() const { return drag_ != Drag::none; }

  [[nodiscard]] bool take_test_request();
  [[nodiscard]] bool take_exit_request();
  [[nodiscard]] const Image& image();

  [[nodiscard]] std::uint64_t revision() const { return image_revision_; }

  [[nodiscard]] MapDocument& document() { return document_; }

  [[nodiscard]] const MapDocument& document() const { return document_; }

  [[nodiscard]] EditorLayer layer() const { return layer_; }

  [[nodiscard]] EditorTool tool() const { return tool_; }

  [[nodiscard]] double zoom() const { return zoom_; }

  [[nodiscard]] const std::string& status() const { return status_; }

  void set_status(std::string message) {
    status_ = std::move(message);
    mark_dirty();
  }

 private:
  enum class FileMode { none, load, save };
  enum class Drag { none, paint, rectangle, pan };

  struct Point {
    int x{}, y{};
  };

  [[nodiscard]] Vec2 logical(Vec2 drawable) const;
  [[nodiscard]] Point tile_at(Vec2 logical) const;
  [[nodiscard]] bool in_canvas(Vec2 logical) const;
  [[nodiscard]] std::uint32_t choice_color(EditorLayer layer, const EditorChoice& choice) const;
  [[nodiscard]] const Image* thumbnail(EditorLayer layer, int type);
  void blit_thumbnail(const Image& thumbnail, double x, double y, double size, double opacity = 1,
                      double clip_x = 0, double clip_y = 0, double clip_width = 1e9,
                      double clip_height = 1e9);
  void update_filtered();
  void set_layer(EditorLayer layer);
  void choose_tool(EditorTool tool);
  void select_palette(Vec2 logical);
  void pointer_down(Vec2 logical, int button, bool shift, bool control);
  void pointer_move(Vec2 logical, bool shift, bool control);
  void pointer_up(Vec2 logical);
  void wheel(Vec2 logical, double steps);
  void key(EditorInputKey key, bool down, bool repeat);
  void append_text(std::string_view text);
  void close_name(bool commit);
  void complete_file_dialog();
  void activate_button(Vec2 logical);
  void mark_dirty();
  void render();
  void render_canvas();
  void render_palette();
  void render_toolbar();
  void render_dialog();
  void fill(double x, double y, double width, double height, std::uint32_t color);
  void text(std::string_view value, int size, std::uint32_t color, double x, double y,
            bool bold = false);
  void outline(double x, double y, double width, double height, std::uint32_t color);

  const Catalog& catalog_;
  const FontBook& fonts_;
  const HudArt* art_{};
  MapDocument document_;
  Image image_;
  std::unordered_map<std::uint32_t, Image> thumbnails_;
  double drawable_width_{800}, drawable_height_{600}, scale_{1};
  double zoom_{1};
  Vec2 center_{128, 128};
  EditorLayer layer_{EditorLayer::ground};
  EditorTool tool_{EditorTool::draw};
  std::size_t selected_{};
  std::vector<std::size_t> filtered_;
  std::size_t palette_row_{};
  std::string filter_, status_{"Draw on the map, then save it or press T to test it."};
  std::string hover_choice_, hover_tile_;
  Vec2 pointer_{};
  bool pointer_valid_{}, shift_down_{}, control_down_{}, shift_rect_{};
  Drag drag_{Drag::none};
  bool drag_erase_{};
  Point drag_start_{}, drag_last_{};
  Vec2 drag_pointer_{};
  Vec2 pan_center_{};
  bool search_focus_{}, name_focus_{};
  Point name_tile_{};
  std::string name_before_;
  FileMode file_mode_{FileMode::none};
  std::filesystem::path file_path_{"map.jm"};
  std::string file_input_;
  bool file_focus_{};
  bool test_request_{}, exit_request_{};
  bool dirty_{true};
  std::uint64_t image_revision_{};
};
}  // namespace realm
