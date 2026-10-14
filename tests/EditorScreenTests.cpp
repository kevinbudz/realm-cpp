#include "realm/mapeditor/EditorScreen.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/objects/Catalog.hpp"
#include "flash_assets.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void send(realm::EditorScreen& editor, realm::EditorInput event) { editor.handle(event); }
void key(realm::EditorScreen& editor, realm::EditorInputKey value) {
  realm::EditorInput event;
  event.kind = realm::EditorInput::Kind::key_down;
  event.key = value;
  send(editor, std::move(event));
}
void pointer(realm::EditorScreen& editor, realm::EditorInput::Kind kind, double x, double y, int button = 1,
             bool shift = false, bool control = false) {
  realm::EditorInput event;
  event.kind = kind;
  event.point = {x, y};
  event.button = button;
  event.shift = shift;
  event.control = control;
  send(editor, std::move(event));
}
}

int main() {
  try {
    using namespace realm;
    Catalog catalog(flash::source_root);
    FontBook fonts(flash::source_root);
    HudArt art;
    EditorScreen editor(catalog, fonts, flash::source_root, &art);
    editor.resize(800, 600, 1);

    const auto first_ground = editor.document().choices(EditorLayer::ground).front().id;
    pointer(editor, EditorInput::Kind::pointer_down, 400, 334);
    pointer(editor, EditorInput::Kind::pointer_move, 416, 334);
    pointer(editor, EditorInput::Kind::pointer_up, 416, 334);
    check(editor.document().tile(128, 128).ids[0] == first_ground &&
              editor.document().tile(129, 128).ids[0] == first_ground,
          "drag paints a single document transaction");
    pointer(editor, EditorInput::Kind::pointer_down, 400, 334, 3);
    pointer(editor, EditorInput::Kind::pointer_move, 416, 334, 3);
    pointer(editor, EditorInput::Kind::pointer_up, 416, 334, 3);
    check(editor.document().tile(128, 128).ids[0].empty() && editor.document().tile(129, 128).ids[0].empty(),
          "right-button erase remains active across a drag");
    key(editor, EditorInputKey::undo);
    check(editor.document().tile(128, 128).ids[0] == first_ground &&
              editor.document().tile(129, 128).ids[0] == first_ground,
          "right-button drag is one undoable edit");

    pointer(editor, EditorInput::Kind::pointer_down, 725, 90);
    check(editor.layer() == EditorLayer::object, "palette layer tab selects objects");
    const auto object_id = editor.document().choices(EditorLayer::object).front().id;
    pointer(editor, EditorInput::Kind::pointer_down, 400, 334);
    pointer(editor, EditorInput::Kind::pointer_up, 400, 334);
    key(editor, EditorInputKey::name);
    pointer(editor, EditorInput::Kind::pointer_down, 400, 334);
    EditorInput typed;
    typed.kind = EditorInput::Kind::text;
    typed.text = "Editor sign";
    send(editor, std::move(typed));
    key(editor, EditorInputKey::enter);
    check(
        editor.document().tile(128, 128).ids[1] == object_id && editor.document().tile(128, 128).name == "Editor sign",
        "name tool edits object properties in one transaction");
    key(editor, EditorInputKey::name);
    pointer(editor, EditorInput::Kind::pointer_down, 400, 334);
    typed.text = "\xC3\xA9";
    send(editor, typed);
    key(editor, EditorInputKey::backspace);
    key(editor, EditorInputKey::enter);
    check(editor.document().tile(128, 128).name == "Editor sign", "backspace removes a complete UTF-8 code point");
    key(editor, EditorInputKey::name);
    pointer(editor, EditorInput::Kind::pointer_down, 400, 334);
    typed.text = " focused";
    send(editor, typed);
    EditorInput focus_lost;
    focus_lost.kind = EditorInput::Kind::focus_lost;
    send(editor, focus_lost);
    check(editor.document().tile(128, 128).name == "Editor sign focused",
          "focus loss commits and closes an in-progress name edit");
    key(editor, EditorInputKey::undo);
    check(editor.document().tile(128, 128).name == "Editor sign",
          "focus-lost name edit remains a single undoable transaction");
    key(editor, EditorInputKey::redo);

    pointer(editor, EditorInput::Kind::pointer_down, 675, 90);
    key(editor, EditorInputKey::draw);
    pointer(editor, EditorInput::Kind::pointer_down, 368, 334, 1, true);
    pointer(editor, EditorInput::Kind::pointer_move, 400, 334, 1, true);
    pointer(editor, EditorInput::Kind::pointer_up, 400, 334);
    check(!editor.document().tile(126, 128).ids[0].empty() && !editor.document().tile(127, 128).ids[0].empty(),
          "shift drag paints a bounded rectangle");
    key(editor, EditorInputKey::undo);
    check(editor.document().tile(126, 128).ids[0].empty() && !editor.document().tile(128, 128).ids[0].empty(),
          "keyboard undo reverts the whole rectangle gesture");

    EditorInput zoom;
    zoom.kind = EditorInput::Kind::wheel;
    zoom.point = {400, 334};
    zoom.wheel = 2;
    send(editor, std::move(zoom));
    check(editor.zoom() > 1, "wheel zooms the map around the pointer");
    EditorInput test;
    test.kind = EditorInput::Kind::key_down;
    test.key = EditorInputKey::test;
    send(editor, std::move(test));
    check(editor.take_test_request() && !editor.take_test_request(), "test command is a one-shot request");
    pointer(editor, EditorInput::Kind::pointer_down, 400, 334);
    check(editor.captured(), "painting begins before map-test shortcut");
    key(editor, EditorInputKey::test);
    check(editor.take_test_request() && !editor.captured(), "testing closes an active editor gesture");

    const auto path = std::filesystem::temp_directory_path() / "realm-editor-screen-test.jm";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    key(editor, EditorInputKey::save);
    check(editor.wants_text(), "save path dialog takes text focus");
    EditorInput clear_path;
    clear_path.kind = EditorInput::Kind::key_down;
    clear_path.key = EditorInputKey::backspace;
    for (int i = 0; i < 6; ++i) send(editor, clear_path);
    EditorInput path_text;
    path_text.kind = EditorInput::Kind::text;
    path_text.text = path.string();
    send(editor, std::move(path_text));
    key(editor, EditorInputKey::enter);
    check(std::filesystem::exists(path) && !editor.document().dirty(), "save dialog writes a clean JM file");
    editor.document().clear();
    key(editor, EditorInputKey::load);
    key(editor, EditorInputKey::enter);
    check(!editor.document().empty() && !editor.document().dirty(), "load dialog restores the saved map");
    EditorInput lost_file_focus;
    lost_file_focus.kind = EditorInput::Kind::focus_lost;
    key(editor, EditorInputKey::load);
    send(editor, lost_file_focus);
    key(editor, EditorInputKey::clear);
    key(editor, EditorInputKey::test);
    key(editor, EditorInputKey::back);
    check(!editor.document().empty() && !editor.take_test_request() && !editor.take_exit_request(),
          "unfocused file modal traps editor shortcuts");
    key(editor, EditorInputKey::escape);
    check(!editor.take_exit_request(), "Escape closes an unfocused file modal without leaving the editor");
    std::filesystem::remove(path, ignored);

    const auto& bitmap = editor.image();
    check(bitmap.width == 800 && bitmap.height == 600 && bitmap.pixel_scale == 1 && editor.revision() > 0,
          "retained editor bitmap has the expected logical surface");
    editor.resize(800, 1200, 2);
    const auto& resized = editor.image();
    check(resized.width == 1600 && resized.height == 1200 && resized.pixel_scale == 2,
          "editor bitmap follows HiDPI resize");
    pointer(editor, EditorInput::Kind::pointer_down, 800, 668);
    check(editor.captured(), "canvas gesture is captured before resize");
    editor.resize(800, 600, 1);
    check(!editor.captured(), "resize cancels stale pointer gestures");
    pointer(editor, EditorInput::Kind::pointer_down, 400, 334);
    key(editor, EditorInputKey::escape);
    check(!editor.captured() && editor.take_exit_request(), "Escape closes an active gesture before exiting");
    EditorScreen filtered_editor(catalog, fonts, flash::source_root);
    const auto last_ground = filtered_editor.document().choices(EditorLayer::ground).back().id;
    pointer(filtered_editor, EditorInput::Kind::pointer_down, 680, 116);
    EditorInput filter_text;
    filter_text.kind = EditorInput::Kind::text;
    filter_text.text = last_ground;
    send(filtered_editor, filter_text);
    key(filtered_editor, EditorInputKey::enter);
    pointer(filtered_editor, EditorInput::Kind::pointer_down, 400, 334);
    pointer(filtered_editor, EditorInput::Kind::pointer_up, 400, 334);
    check(filtered_editor.document().tile(128, 128).ids[0] == last_ground,
          "filtering selects a matching visible palette entry for painting");
    std::cout << "Map editor gestures, tools, path dialog, rendering and resize checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
