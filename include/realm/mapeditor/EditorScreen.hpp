#pragma once
// Flash: com/company/assembleegameclient/mapeditor/EditingScreen.as, MEMap.as, Chooser.as, and
// com/company/assembleegameclient/ui/dropdown/DropDown.as
#include "realm/mapeditor/MapDocument.hpp"
#include "realm/assets/FontBook.hpp"
#include "realm/assets/Image.hpp"
#include "realm/platform/FileDialog.hpp"
#include "realm/ui/Scrollbar.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace realm {
class Catalog;
class HudArt;

// Flash MECommandMenu tools in menu order. Draw is selected when the editor opens.
enum class EditorTool { draw, erase, sample, edit };
enum class EditorInputKey {
  none,
  escape,
  enter,
  backspace,
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
  edit,
  clear,
  load,
  save,
  test,
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

// Headless Flash map-editor screen (EditingScreen.as) and input state. The bitmap is rebuilt only
// when the document, the view or the density changes; the map view is cached on the same terms.
class EditorScreen {
 public:
  EditorScreen(const Catalog& catalog, const FontBook& fonts,
               const std::filesystem::path& flash_root, const HudArt* art = nullptr);

  void resize(double drawable_width, double drawable_height, double pixel_scale);
  void handle(const EditorInput& event);
  void cancel_pointer();
  // Polls the system file picker and applies its result. Startup calls it every frame.
  void update();
  // Replaces the system file picker; tests use a fake that answers without a GUI.
  void use_file_dialog(std::unique_ptr<platform::FileDialog> dialog);
  [[nodiscard]] bool covers(Vec2 drawable_point) const;
  [[nodiscard]] bool wants_text() const;

  [[nodiscard]] bool captured() const {
    return button_down_ || scrolling_ || press_.kind != Press::Kind::none;
  }

  [[nodiscard]] bool take_test_request();
  [[nodiscard]] bool take_exit_request();
  [[nodiscard]] const Image& image();

  [[nodiscard]] std::uint64_t revision() const { return image_revision_; }

  [[nodiscard]] MapDocument& document() { return document_; }

  [[nodiscard]] const MapDocument& document() const { return document_; }

  [[nodiscard]] EditorLayer layer() const { return layer_; }

  [[nodiscard]] EditorTool tool() const { return command_; }

  [[nodiscard]] double zoom() const { return zoom_; }

  // The file name Save proposes; Clear resets it to map.jm.
  [[nodiscard]] const std::filesystem::path& save_path() const { return file_path_; }

  [[nodiscard]] const std::string& status() const { return status_; }

  // A non-empty message opens the "Map editor" error frame; an empty one only clears the status.
  void set_status(std::string message);

  [[nodiscard]] bool tooltip_visible() const;
  [[nodiscard]] bool edit_open() const { return edit_open_; }
  [[nodiscard]] const std::string& edit_name() const { return edit_name_; }
  [[nodiscard]] bool file_dialog_open() const { return dialog_mode_ != FileMode::none; }
  [[nodiscard]] bool error_open() const { return error_open_; }
  [[nodiscard]] const std::string& error_message() const { return error_message_; }

 private:
  enum class FileMode { none, load, save };
  // MECommandMenu items. The first four are the tools, in the same order as EditorTool.
  enum class MenuItem { draw, erase, sample, edit, undo, redo, clear, load, save, test };
  enum class SpriteKind { ground, object, element };
  // The frames that take the screen while open, in precedence order: error over edit.
  enum class Modal { none, edit, error };

  // A press activates its target only when it is released over the same target (Flash CLICK).
  struct Press {
    enum class Kind { none, menu, layer, layer_item, dialog };
    Kind kind{Kind::none};
    int index{};
  };

  struct Point {
    int x{}, y{};
  };

  struct TileRect {
    int x{}, y{}, width{}, height{};
  };

  struct Area {
    double x{}, y{}, width{}, height{};
  };

  // ElementTypeToolTip content: the title (the type id) and the description, when the type has one.
  struct ElementTip {
    std::string title;
    std::string body;
    bool has_body{};
  };

  struct MapKey {
    std::uint64_t revision{};
    int x{}, y{};
    double zoom{}, scale{};
    bool operator==(const MapKey&) const = default;
  };

  // One Flash Chooser per layer, each with its own selection, scroll position and element art.
  struct Chooser {
    EditorLayer layer{EditorLayer::ground};
    std::size_t selected{};
    Scrollbar scroll{20, 472};
    std::vector<std::optional<Image>> art;
  };

  [[nodiscard]] Vec2 logical(Vec2 drawable) const;
  [[nodiscard]] bool in_canvas(Vec2 logical) const;
  [[nodiscard]] bool over_canvas() const;
  [[nodiscard]] int size_tiles() const;
  [[nodiscard]] Point mouse_tile() const;
  [[nodiscard]] TileRect mouse_rect() const;
  [[nodiscard]] bool can_move() const;
  void move_view(int dx, int dy);
  void zoom_by(double factor);
  [[nodiscard]] Chooser& current_chooser() { return choosers_[static_cast<std::size_t>(layer_)]; }
  [[nodiscard]] const Chooser& current_chooser() const {
    return choosers_[static_cast<std::size_t>(layer_)];
  }
  [[nodiscard]] double scroll_offset(const Chooser& chooser) const;
  [[nodiscard]] Vec2 scroll_local(Vec2 point) const;
  [[nodiscard]] std::optional<std::size_t> element_at(Vec2 point) const;
  [[nodiscard]] std::optional<MenuItem> menu_at(Vec2 point) const;
  [[nodiscard]] std::optional<std::size_t> layer_item_at(Vec2 point) const;
  [[nodiscard]] bool in_layer_list(Vec2 point) const;
  [[nodiscard]] Modal modal() const;
  [[nodiscard]] double modal_height(Modal kind) const;
  [[nodiscard]] std::array<std::string_view, 2> button_labels(Modal kind) const;
  [[nodiscard]] Area button_area(Modal kind, std::size_t index) const;
  [[nodiscard]] std::optional<std::size_t> dialog_button_at(Vec2 point) const;
  void dialog_action(std::size_t index);
  [[nodiscard]] std::optional<ElementTip> element_tip(std::size_t index) const;
  [[nodiscard]] std::vector<std::string> lines_of(std::string_view value, int size, bool bold,
                                                  int width) const;
  void refresh_tooltip();
  [[nodiscard]] static std::vector<Point> tiles_in(TileRect rect);
  void activate(MenuItem item);
  void select_layer(EditorLayer layer);
  void dispatch(const std::vector<Point>& tiles);
  void sample(Point tile);
  void clear_map();
  void start_file_dialog(FileMode mode);
  void finish_file_dialog(FileMode mode, const std::filesystem::path& path);
  void open_edit(const std::vector<Point>& tiles);
  void close_edit(bool save);
  void close_error();
  void backspace();
  void append_text(std::string_view text);
  void pointer_down(Vec2 logical, int button, bool shift);
  void pointer_move(Vec2 logical, bool shift, bool control);
  void pointer_up(Vec2 logical);
  void wheel(Vec2 logical, double steps);
  void key(EditorInputKey key, bool down);
  void mark_dirty();
  [[nodiscard]] const Image* sprite(SpriteKind kind, int type);
  [[nodiscard]] const Image& element_art(std::size_t index);
  void clear_art();
  void build_map();
  void draw_tile(int x, int y, double tile);
  void render();
  void render_map();
  void render_menu();
  void render_layers();
  void render_chooser();
  void render_info();
  void render_modal();
  void render_tooltip();
  void paragraphs(std::string_view value, double x, double y, double hanging);
  [[nodiscard]] Area clipped(double x, double y, double width, double height) const;
  void fill(double x, double y, double width, double height, std::uint32_t color);
  void outline(double x, double y, double width, double height, std::uint32_t color,
               double thickness = 1);
  void tint(double x, double y, double width, double height, std::uint32_t color, double alpha);
  void text(std::string_view value, int size, std::uint32_t color, double x, double y,
            bool bold = false, bool shadow = false);

  const Catalog& catalog_;
  const FontBook& fonts_;
  const HudArt* art_{};
  MapDocument document_;
  Image image_;
  // Flash MEMap_transbackgroundEmbed_, premultiplied.
  Image checker_;
  // Zoomed, premultiplied view of the map at the current scale; rebuilt when map_key_ changes.
  Image map_;
  std::optional<MapKey> map_key_;
  std::unordered_map<std::uint64_t, std::optional<Image>> sprites_;
  std::unordered_map<std::string, std::uint32_t> region_colors_;
  std::array<Chooser, 3> choosers_;
  double drawable_width_{800}, drawable_height_{600}, scale_{1};
  double zoom_{1};
  Point origin_{};
  EditorTool command_{EditorTool::draw};
  EditorLayer layer_{EditorLayer::ground};
  bool dropdown_open_{};
  Press press_{};
  Vec2 pointer_{};
  bool pointer_valid_{};
  bool button_down_{};
  bool scrolling_{};
  // Flash MEMap rect anchor (Shift) and move anchor (Ctrl).
  std::optional<Point> rect_anchor_, move_anchor_;
  // Flash EditTileProperties: the tiles an Edit applies to and the name typed in its frame.
  std::vector<Point> edit_tiles_;
  bool edit_open_{};
  std::string edit_name_;
  Area clip_{0, 0, 800, 600};
  // The mode of the picker that is open, if any. While it is open the editor takes no input.
  FileMode dialog_mode_{FileMode::none};
  std::unique_ptr<platform::FileDialog> dialog_;
  std::filesystem::path file_path_{"map.jm"};
  // The "Map editor" error frame opened by set_status, and the element under the pointer whose
  // ToolTip is shown.
  bool error_open_{};
  std::string error_message_;
  std::optional<std::size_t> tooltip_;
  bool test_request_{}, exit_request_{};
  std::string status_;
  bool dirty_{true};
  std::uint64_t image_revision_{};
};
}  // namespace realm
