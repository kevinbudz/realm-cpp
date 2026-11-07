#include "realm/mapeditor/EditorScreen.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/objects/Catalog.hpp"
#include "flash_assets.hpp"
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

// Stands in for the system picker. start() records the request; the answer set by choose() is
// delivered by the next poll(), as the real picker's result is. A nullopt answer is a cancel.
class FakePicker : public realm::platform::FileDialog {
 public:
  using Mode = realm::platform::FileDialog::Mode;
  using Result = realm::platform::FileDialog::Result;

  [[nodiscard]] bool available() const override { return available_; }

  bool start(Mode mode, std::string title, std::filesystem::path suggested, std::string,
             std::string) override {
    if (running_) {
      return false;
    }
    running_ = true;
    ++starts;
    last_mode = mode;
    last_title = std::move(title);
    last_suggested = std::move(suggested);
    return true;
  }

  std::optional<Result> poll() override {
    if (!running_ || !answer_) {
      return std::nullopt;
    }
    running_ = false;
    return std::exchange(answer_, std::nullopt);
  }

  [[nodiscard]] bool running() const override { return running_; }

  void choose(Result value) { answer_ = std::move(value); }

  bool available_{true};
  int starts{};
  Mode last_mode{Mode::open};
  std::string last_title;
  std::filesystem::path last_suggested;

 private:
  bool running_{};
  std::optional<Result> answer_;
};

void send(realm::EditorScreen& editor, realm::EditorInput event) {
  editor.handle(event);
}

void key(realm::EditorScreen& editor, realm::EditorInputKey value) {
  realm::EditorInput event;
  event.kind = realm::EditorInput::Kind::key_down;
  event.key = value;
  send(editor, std::move(event));
}

void key_up(realm::EditorScreen& editor, realm::EditorInputKey value) {
  realm::EditorInput event;
  event.kind = realm::EditorInput::Kind::key_up;
  event.key = value;
  send(editor, std::move(event));
}

void pointer(realm::EditorScreen& editor, realm::EditorInput::Kind kind, double x, double y,
             int button = 1, bool shift = false, bool control = false) {
  realm::EditorInput event;
  event.kind = kind;
  event.point = {x, y};
  event.button = button;
  event.shift = shift;
  event.control = control;
  send(editor, std::move(event));
}

void click(realm::EditorScreen& editor, double x, double y) {
  pointer(editor, realm::EditorInput::Kind::pointer_down, x, y);
  pointer(editor, realm::EditorInput::Kind::pointer_up, x, y);
}

void text(realm::EditorScreen& editor, std::string value) {
  realm::EditorInput event;
  event.kind = realm::EditorInput::Kind::text;
  event.text = std::move(value);
  send(editor, std::move(event));
}

// Logical center of a tile while the view is at its initial origin (112, 112) and zoom 1.
realm::Vec2 tile_center(int x, int y) {
  return {144 + (x - 112) * 16 + 8.0, 78 + (y - 112) * 16 + 8.0};
}

bool painted(const realm::EditorScreen& editor, int x, int y, realm::EditorLayer layer) {
  return !editor.document().tile(x, y).ids[static_cast<std::size_t>(layer)].empty();
}

// Whether any tile in a column holds a ground or object; the pointer's row depends on the pan.
bool column_painted(const realm::EditorScreen& editor, int x) {
  for (int y = 0; y < realm::MapDocument::extent; ++y) {
    if (painted(editor, x, y, realm::EditorLayer::ground) ||
        painted(editor, x, y, realm::EditorLayer::object)) {
      return true;
    }
  }
  return false;
}

// Flash MECommandMenu item centers: (D)raw 90, (E)rase 120, S(A)mple 150, Ed(I)t 180, (U)ndo 210,
// (R)edo 240, (C)lear 270, (L)oad 330, (S)ave 360, (T)est 390.
constexpr double menu_x = 55;
constexpr double draw_y = 90, erase_y = 120, sample_y = 150, edit_y = 180, clear_y = 270,
                 load_y = 330, save_y = 360, test_y = 390;
// Layer DropDown: the closed head at (660, 78), list items 26 px apart.
constexpr double layer_head_x = 700, layer_head_y = 90;

void open_objects(realm::EditorScreen& editor) {
  click(editor, layer_head_x, layer_head_y);
  click(editor, layer_head_x, 116);
}

// Flash Frame buttons in the frame's logical coordinates: Cancel left, the Save, Load or Ok right.
constexpr double cancel_x = 380, cancel_y = 352, confirm_x = 500, confirm_y = 352;
// The error frame is one line tall, so its Ok button sits higher.
constexpr double ok_x = 510, ok_y = 316;

// Pixel of the straight-alpha 800 wide editor bitmap.
std::uint32_t rgb(const realm::Image& image, int x, int y) {
  const auto* p = &image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4];
  return (std::uint32_t{p[0]} << 16) | (std::uint32_t{p[1]} << 8) | p[2];
}

bool named(const realm::EditorScreen& editor, int x, int y, std::optional<std::string> name) {
  return editor.document().tile(x, y).name == name;
}
}  // namespace

int main() {
  try {
    using namespace realm;
    Catalog catalog(flash::source_root);
    FontBook fonts(flash::source_root);
    HudArt art;
    EditorScreen editor(catalog, fonts, flash::source_root, &art);
    editor.resize(800, 600, 1);

    check(editor.tool() == EditorTool::draw && editor.layer() == EditorLayer::ground,
          "the editor opens on Draw with the Ground layer");
    check(editor.covers({400, 40}) && !editor.covers({400, 20}) && !editor.covers({801, 300}),
          "the editor covers the design area below the account header");

    // MECommandMenu: items are 80x25 at x 15 with 30 px steps and a break before Load.
    click(editor, menu_x, erase_y);
    check(editor.tool() == EditorTool::erase, "Erase menu item selects the erase tool");
    click(editor, menu_x, 105);
    check(editor.tool() == EditorTool::erase, "the gap between menu items does nothing");
    click(editor, menu_x, sample_y);
    check(editor.tool() == EditorTool::sample, "Sample menu item selects the sample tool");
    click(editor, menu_x, edit_y);
    check(editor.tool() == EditorTool::edit, "Edit menu item selects the edit tool");
    click(editor, menu_x, draw_y);
    check(editor.tool() == EditorTool::draw, "Draw menu item selects the draw tool");
    key(editor, EditorInputKey::erase);
    check(editor.tool() == EditorTool::erase, "E key selects erase");
    key(editor, EditorInputKey::draw);

    // DropDown: the closed head opens the list; the list item selects and closes.
    click(editor, layer_head_x, layer_head_y);
    check(editor.layer() == EditorLayer::ground, "opening the layer list does not change layers");
    click(editor, layer_head_x, 140);
    check(editor.layer() == EditorLayer::region, "the third list item selects Regions");
    open_objects(editor);
    check(editor.layer() == EditorLayer::object, "the second list item selects Objects");
    click(editor, layer_head_x, layer_head_y);
    click(editor, layer_head_x, 90);
    check(editor.layer() == EditorLayer::ground, "the first list item selects Ground");

    // ToolTip: hovering an element shows its type tip; rolling out removes it, and so does a click on
    // the layer head or any change of chooser. The Ground tip's box is 192 px wide (180 + 12) and its
    // left edge sits 7 px left of a pointer right of center, so (477, 170) is inside its fill.
    pointer(editor, EditorInput::Kind::pointer_move, 674, 130);
    check(editor.tooltip_visible(), "hovering a Ground element shows its tooltip");
    check(rgb(editor.image(), 477, 170) == 0x363636, "the tooltip box is drawn at the flipped position");
    pointer(editor, EditorInput::Kind::pointer_move, 400, 300);
    check(!editor.tooltip_visible(), "rolling out of the element removes the tooltip");
    pointer(editor, EditorInput::Kind::pointer_move, 674, 130);
    click(editor, layer_head_x, layer_head_y);
    check(!editor.tooltip_visible(), "clicking the layer head removes the tooltip");
    click(editor, layer_head_x, 140);
    pointer(editor, EditorInput::Kind::pointer_move, 674, 130);
    check(editor.tooltip_visible(), "a Region element shows its tooltip");
    click(editor, layer_head_x, layer_head_y);
    click(editor, layer_head_x, 90);
    pointer(editor, EditorInput::Kind::pointer_move, 674, 130);
    check(editor.tooltip_visible(), "the Ground element shows its tooltip again");
    click(editor, layer_head_x, layer_head_y);
    click(editor, layer_head_x, 90);
    check(!editor.tooltip_visible(), "a change of chooser removes the tooltip");
    pointer(editor, EditorInput::Kind::pointer_move, tile_center(120, 120).x,
            tile_center(120, 120).y);

    // Undo runs one step per dispatch: the press and each move dispatch one tile.
    const auto a = tile_center(120, 120), b = tile_center(121, 120), c = tile_center(122, 120);
    const auto ground_id = editor.document().choices(EditorLayer::ground).front().id;
    pointer(editor, EditorInput::Kind::pointer_move, a.x, a.y);
    pointer(editor, EditorInput::Kind::pointer_down, a.x, a.y);
    pointer(editor, EditorInput::Kind::pointer_move, b.x, b.y);
    pointer(editor, EditorInput::Kind::pointer_move, c.x, c.y);
    pointer(editor, EditorInput::Kind::pointer_up, c.x, c.y);
    check(editor.document().tile(120, 120).ids[0] == ground_id &&
              editor.document().tile(122, 120).ids[0] == ground_id,
          "a drag paints each dispatched tile");
    key(editor, EditorInputKey::undo);
    check(!painted(editor, 122, 120, EditorLayer::ground) &&
              painted(editor, 121, 120, EditorLayer::ground),
          "undo reverts only the last dispatch of a drag");
    key(editor, EditorInputKey::undo);
    check(!painted(editor, 121, 120, EditorLayer::ground) &&
              painted(editor, 120, 120, EditorLayer::ground),
          "each dispatch is its own undo step");
    key(editor, EditorInputKey::undo);
    check(!painted(editor, 120, 120, EditorLayer::ground), "the press dispatch is undoable");
    key(editor, EditorInputKey::redo);
    check(painted(editor, 120, 120, EditorLayer::ground), "redo restores the press dispatch");
    key(editor, EditorInputKey::undo);

    // Shift anchors the rectangle at the pointer tile; a press with Shift dispatches it at once.
    pointer(editor, EditorInput::Kind::pointer_move, a.x, a.y);
    key(editor, EditorInputKey::shift);
    pointer(editor, EditorInput::Kind::pointer_move, tile_center(122, 122).x,
            tile_center(122, 122).y, 1, true);
    pointer(editor, EditorInput::Kind::pointer_down, tile_center(122, 122).x,
            tile_center(122, 122).y, 1, true);
    pointer(editor, EditorInput::Kind::pointer_up, tile_center(122, 122).x,
            tile_center(122, 122).y);
    check(painted(editor, 120, 120, EditorLayer::ground) &&
              painted(editor, 122, 122, EditorLayer::ground) &&
              painted(editor, 121, 121, EditorLayer::ground),
          "a Shift press paints the anchored rectangle");
    key(editor, EditorInputKey::undo);
    check(!painted(editor, 121, 121, EditorLayer::ground) &&
              !painted(editor, 120, 120, EditorLayer::ground),
          "the Shift rectangle is one undo step");
    key_up(editor, EditorInputKey::shift);
    pointer(editor, EditorInput::Kind::pointer_move, a.x, a.y);

    // Ctrl pans with no button: the anchor tile stays under the pointer.
    key(editor, EditorInputKey::control);
    pointer(editor, EditorInput::Kind::pointer_move, a.x, a.y, 1, false, true);
    pointer(editor, EditorInput::Kind::pointer_move, a.x - 32, a.y, 1, false, true);
    key_up(editor, EditorInputKey::control);
    click(editor, a.x - 32, a.y);
    check(painted(editor, 120, 120, EditorLayer::ground),
          "Ctrl pan without a button keeps the anchor tile under the pointer");
    key(editor, EditorInputKey::undo);
    // The pan moved the view two tiles; the arrows return it to the origin the tile helper assumes.
    key(editor, EditorInputKey::left);
    key(editor, EditorInputKey::left);
    pointer(editor, EditorInput::Kind::pointer_move, a.x, a.y, 1, false, false);

    // Sample with the object layer switches to Draw and selects the sampled object.
    key(editor, EditorInputKey::draw);
    open_objects(editor);
    click(editor, 726, 130);
    const auto object_one = editor.document().choices(EditorLayer::object)[1].id;
    click(editor, tile_center(125, 125).x, tile_center(125, 125).y);
    check(editor.document().tile(125, 125).ids[1] == object_one, "draw paints the chooser object");
    click(editor, 681, 132);
    click(editor, menu_x, sample_y);
    click(editor, tile_center(127, 127).x, tile_center(127, 127).y);
    check(editor.tool() == EditorTool::sample, "sample on an empty tile keeps the sample tool");
    click(editor, tile_center(125, 125).x, tile_center(125, 125).y);
    check(editor.tool() == EditorTool::draw, "sampling a tile switches to Draw");
    click(editor, tile_center(126, 125).x, tile_center(126, 125).y);
    check(editor.document().tile(126, 125).ids[1] == object_one,
          "sample selects the sampled object in the chooser");
    key(editor, EditorInputKey::undo);
    key(editor, EditorInputKey::undo);

    // Choosers: element i sits at x 664 + 52 * (i % 2), y 120 + 50 * (i / 2).
    click(editor, 674, 130);
    click(editor, tile_center(120, 120).x, tile_center(120, 120).y);
    check(editor.document().tile(120, 120).ids[1] ==
              editor.document().choices(EditorLayer::object)[0].id,
          "the first element is the first object");
    click(editor, 726, 130);
    click(editor, tile_center(121, 120).x, tile_center(121, 120).y);
    check(editor.document().tile(121, 120).ids[1] == object_one,
          "the second column is the second element");
    click(editor, 689, 195);
    click(editor, tile_center(122, 120).x, tile_center(122, 120).y);
    check(editor.document().tile(122, 120).ids[1] ==
              editor.document().choices(EditorLayer::object)[2].id,
          "the next row starts 50 px lower");
    key(editor, EditorInputKey::clear);
    check(editor.document().empty(), "clear removes every tile");
    key(editor, EditorInputKey::undo);
    check(!editor.document().empty(), "clear is one undoable step");
    key(editor, EditorInputKey::clear);

    // Wheel over the chooser scrolls it, so a row-0 position now selects a later element.
    EditorInput scroll;
    scroll.kind = EditorInput::Kind::wheel;
    scroll.point = {700, 300};
    scroll.wheel = -3;
    send(editor, scroll);
    click(editor, 674, 130);
    click(editor, tile_center(124, 120).x, tile_center(124, 120).y);
    check(editor.document().tile(124, 120).ids[1] != editor.document().choices(EditorLayer::object)[0].id,
          "wheel scrolls the chooser");
    key(editor, EditorInputKey::clear);

    // Zoom: the wheel and = double, - halves, and the view is clamped to the 256 tile map.
    EditorInput zoom;
    zoom.kind = EditorInput::Kind::wheel;
    zoom.point = {400, 334};
    zoom.wheel = 1;
    send(editor, zoom);
    check(editor.zoom() == 2, "a wheel step zooms in by two");
    key(editor, EditorInputKey::zoom_out);
    check(editor.zoom() == 1, "- zooms out by half");
    key(editor, EditorInputKey::zoom_in);
    for (int i = 0; i < 10; ++i) {
      key(editor, EditorInputKey::zoom_in);
    }
    check(editor.zoom() == 16, "zoom clamps at 16");
    for (int i = 0; i < 10; ++i) {
      key(editor, EditorInputKey::zoom_out);
    }
    check(editor.zoom() == .125, "zoom clamps at 1/8");
    key(editor, EditorInputKey::zoom_in);
    key(editor, EditorInputKey::zoom_in);
    key(editor, EditorInputKey::zoom_in);
    check(editor.zoom() == 1, "zoom returns to 1 after three steps back");

    // Arrows clamp the view at the map edge (zoom 1 shows 32 tiles).
    for (int i = 0; i < 300; ++i) {
      key(editor, EditorInputKey::left);
    }
    click(editor, 146, tile_center(120, 120).y);
    check(column_painted(editor, 0), "the view clamps at the left edge of the map");
    key(editor, EditorInputKey::undo);
    for (int i = 0; i < 300; ++i) {
      key(editor, EditorInputKey::right);
    }
    click(editor, 654, tile_center(120, 120).y);
    check(column_painted(editor, MapDocument::extent - 1),
          "the view clamps at the right edge of the map");
    key(editor, EditorInputKey::undo);
    key(editor, EditorInputKey::clear);
    // The right clamp leaves the view at 224; step back to the origin the tile helper assumes.
    for (int i = 0; i < 112; ++i) {
      key(editor, EditorInputKey::left);
    }

    // Edit: a Shift rectangle opens Tile properties for all its tiles. The frame traps other keys.
    open_objects(editor);
    click(editor, 726, 130);
    key(editor, EditorInputKey::edit);
    pointer(editor, EditorInput::Kind::pointer_move, tile_center(118, 118).x,
            tile_center(118, 118).y, 1, true);
    pointer(editor, EditorInput::Kind::pointer_move, tile_center(119, 119).x,
            tile_center(119, 119).y, 1, true);
    pointer(editor, EditorInput::Kind::pointer_down, tile_center(119, 119).x,
            tile_center(119, 119).y, 1, true);
    pointer(editor, EditorInput::Kind::pointer_up, tile_center(119, 119).x,
            tile_center(119, 119).y);
    check(editor.edit_open() && editor.wants_text() && editor.edit_name().empty(),
          "Edit on a Shift rectangle opens an empty Tile properties frame");
    key(editor, EditorInputKey::draw);
    check(editor.tool() == EditorTool::edit && editor.edit_open(),
          "the Tile properties frame blocks command hotkeys");
    text(editor, "Editor sign");
    key(editor, EditorInputKey::enter);
    check(!editor.edit_open() && named(editor, 118, 118, "Editor sign") &&
              named(editor, 119, 118, "Editor sign") && named(editor, 118, 119, "Editor sign") &&
              named(editor, 119, 119, "Editor sign"),
          "Save names every dispatched tile");
    key(editor, EditorInputKey::undo);
    check(named(editor, 118, 118, std::nullopt) && named(editor, 119, 119, std::nullopt) &&
              named(editor, 119, 118, std::nullopt) && named(editor, 118, 119, std::nullopt),
          "naming every tile is one undo step");
    key(editor, EditorInputKey::redo);
    check(named(editor, 119, 119, "Editor sign") && named(editor, 118, 119, "Editor sign"),
          "redo restores every name");

    // The frame is prefilled with the first tile's name; Escape closes it without a change.
    key(editor, EditorInputKey::edit);
    click(editor, tile_center(118, 118).x, tile_center(118, 118).y);
    check(editor.edit_name() == "Editor sign", "the frame is prefilled from the tile's name");
    text(editor, "!");
    key(editor, EditorInputKey::escape);
    check(!editor.edit_open() && named(editor, 118, 118, "Editor sign"),
          "Escape cancels the Tile properties frame");

    // Cancel closes without a change.
    key(editor, EditorInputKey::edit);
    click(editor, tile_center(120, 118).x, tile_center(120, 118).y);
    text(editor, "x");
    click(editor, cancel_x, cancel_y);
    check(!editor.edit_open() && !named(editor, 120, 118, "x") &&
              !editor.document().tile(120, 118).name.has_value(),
          "the Cancel button closes the frame without a change");

    // Save with an empty name clears the name.
    key(editor, EditorInputKey::edit);
    click(editor, tile_center(118, 118).x, tile_center(118, 118).y);
    for (int i = 0; i < 20; ++i) {
      key(editor, EditorInputKey::backspace);
    }
    check(editor.edit_name().empty(), "backspace removes the prefilled name");
    click(editor, confirm_x, confirm_y);
    check(!editor.edit_open() && !editor.document().tile(118, 118).name.has_value(),
          "saving an empty name clears the tile's name");
    key(editor, EditorInputKey::undo);
    check(named(editor, 118, 118, "Editor sign"), "clearing a name is undoable");
    key(editor, EditorInputKey::draw);
    // Test and Escape: T is a one-shot request, and Escape exits when no dialog is open.
    key(editor, EditorInputKey::test);
    check(editor.take_test_request() && !editor.take_test_request(),
          "test command is a one-shot request");
    click(editor, menu_x, test_y);
    check(editor.take_test_request(), "the Test menu item requests a map test");
    key(editor, EditorInputKey::escape);
    check(editor.take_exit_request() && !editor.take_exit_request(),
          "Escape leaves the editor");

    // Load and Save go through the system picker. The fake answers on the next update, as the real
    // picker answers on a later frame.
    auto picker = std::make_unique<FakePicker>();
    FakePicker& dialog = *picker;
    editor.use_file_dialog(std::move(picker));
    const auto path = std::filesystem::temp_directory_path() / "realm-editor-screen-test.jm";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    // Save of an empty map is silent: Flash createMapJSON returns null, so no picker opens.
    key(editor, EditorInputKey::clear);
    click(editor, menu_x, save_y);
    check(!editor.file_dialog_open() && dialog.starts == 0, "saving an empty map opens no picker");

    // Save and Clear: Clear resets the save name to map.jm. The map needs a real tile to save.
    click(editor, tile_center(120, 120).x, tile_center(120, 120).y);
    click(editor, menu_x, save_y);
    check(editor.file_dialog_open() && dialog.last_mode == FakePicker::Mode::save &&
              dialog.last_suggested == std::filesystem::path("map.jm"),
          "Save opens a save picker proposing map.jm");
    dialog.choose(path);
    editor.update();
    check(std::filesystem::exists(path) && !editor.document().dirty() && !editor.file_dialog_open(),
          "save picker writes a clean JM file and closes");
    check(editor.save_path() == std::filesystem::path("map.jm"),
          "saving does not change the remembered name");

    click(editor, menu_x, save_y);
    const auto status = editor.status();
    dialog.choose(std::nullopt);
    editor.update();
    check(!editor.file_dialog_open() && !editor.error_open() && editor.status() == status,
          "cancelling the save picker does nothing");

    // A missing picker tool reports through the error frame and opens nothing.
    dialog.available_ = false;
    click(editor, menu_x, load_y);
    check(!editor.file_dialog_open() && editor.error_open() &&
              editor.error_message() == "No system file picker found (install zenity or kdialog).",
          "a missing picker tool shows an error frame");
    click(editor, ok_x, ok_y);
    dialog.available_ = true;

    click(editor, menu_x, load_y);
    check(editor.file_dialog_open() && dialog.last_mode == FakePicker::Mode::open,
          "Load opens an open picker");
    dialog.choose(std::filesystem::path("/realm-no-such-directory/map.jm"));
    editor.update();
    check(editor.error_open() && !editor.document().dirty() && !editor.document().empty() &&
              editor.save_path() == std::filesystem::path("map.jm"),
          "a failed load shows the error frame and keeps the map");
    // The message wraps, which moves the Ok button, so Enter closes the frame.
    key(editor, EditorInputKey::enter);
    check(!editor.error_open(), "Enter closes the error frame");

    key(editor, EditorInputKey::clear);
    check(editor.document().empty(), "Clear empties the map before loading");
    key(editor, EditorInputKey::load);
    check(editor.file_dialog_open(), "the L hotkey opens the load picker");
    dialog.choose(path);
    editor.update();
    check(!editor.file_dialog_open() && !editor.document().empty() && !editor.document().dirty() &&
              editor.save_path() == path,
          "Load restores the saved map and remembers its file");
    click(editor, menu_x, save_y);
    check(dialog.last_suggested == path, "Save proposes the loaded file's name");
    dialog.choose(std::nullopt);
    editor.update();

    // While the picker is open the editor takes no input, except that focus loss still clears the
    // Shift and Ctrl anchors. Escape does not leave the editor either.
    EditorInput lost_focus;
    lost_focus.kind = EditorInput::Kind::focus_lost;
    key(editor, EditorInputKey::load);
    send(editor, lost_focus);
    key(editor, EditorInputKey::clear);
    key(editor, EditorInputKey::test);
    check(!editor.document().empty() && !editor.take_test_request(),
          "an open picker traps editor shortcuts after focus is lost");
    key(editor, EditorInputKey::escape);
    check(!editor.take_exit_request() && editor.file_dialog_open(),
          "Escape does not leave the editor while the picker is open");
    dialog.choose(std::nullopt);
    editor.update();
    check(!editor.file_dialog_open(), "the picker closes once it is answered");
    std::filesystem::remove(path, ignored);

    // set_status opens the Map editor error frame; Ok, Enter and Escape close it.
    editor.set_status("");
    check(!editor.error_open() && editor.status().empty(), "an empty status opens no frame");
    editor.set_status("Create a character before testing a map.");
    check(editor.error_open() && editor.status() == "Create a character before testing a map." &&
              editor.error_message() == editor.status(),
          "set_status opens the error frame with its message");
    click(editor, menu_x, test_y);
    check(!editor.take_test_request(), "the error frame blocks the editor");
    click(editor, ok_x, ok_y);
    check(!editor.error_open() && editor.status() == "Create a character before testing a map.",
          "Ok closes the error frame and keeps the status");
    editor.set_status("first");
    key(editor, EditorInputKey::enter);
    check(!editor.error_open(), "Enter closes the error frame");
    editor.set_status("second");
    key(editor, EditorInputKey::escape);
    check(!editor.error_open() && !editor.take_exit_request(),
          "Escape closes the error frame without leaving the editor");
    key(editor, EditorInputKey::draw);
    const auto& bitmap = editor.image();
    check(bitmap.width == 800 && bitmap.height == 600 && bitmap.pixel_scale == 1 &&
              editor.revision() > 0,
          "retained editor bitmap has the expected logical surface");
    editor.resize(800, 1200, 2);
    const auto& resized = editor.image();
    check(resized.width == 1600 && resized.height == 1200 && resized.pixel_scale == 2,
          "editor bitmap follows HiDPI resize");
    pointer(editor, EditorInput::Kind::pointer_down, 400, 334);
    check(editor.captured(), "canvas gesture is captured before resize");
    editor.resize(800, 600, 1);
    check(!editor.captured(), "resize cancels stale pointer gestures");
    std::cout << "Map editor layout, dropdown, chooser, undo, anchor, zoom, clamp and dialog checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
