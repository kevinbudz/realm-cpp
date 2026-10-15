#include "realm/mapeditor/MapDocument.hpp"
#include "realm/map/World.hpp"
#include "flash_assets.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* reason) {
  if (!value) {
    throw std::runtime_error(reason);
  }
}

template <class F>
void rejects(F action) {
  bool rejected{};
  try {
    action();
  } catch (const std::exception&) {
    rejected = true;
  }
  check(rejected, "invalid editor operation rejected");
}
}

int main() {
  try {
    using namespace realm;
    Catalog catalog(flash::source_root);
    MapDocument document(catalog, flash::source_root);
    check(document.empty() && !document.dirty(), "new editor document is empty and clean");
    check(!document.choices(EditorLayer::ground).empty() &&
              !document.choices(EditorLayer::object).empty() &&
              !document.choices(EditorLayer::region).empty(),
          "all Flash palette layers load");
    const auto ground = document.choices(EditorLayer::ground).front().id;
    document.begin_edit();
    document.paint(128, 128, EditorLayer::ground, ground);
    document.paint(129, 128, EditorLayer::ground, ground);
    document.paint(128, 128, EditorLayer::object, "Sign");
    document.name(128, 128, "Hello|Native");
    document.paint(129, 128, EditorLayer::region, "Spawn");
    document.end_edit();
    check(document.bounds()->width == 2 && document.bounds()->height == 1 && document.dirty(),
          "cropped map bounds");
    const auto json = document.serialize();
    World world(catalog);
    world.load_jm_text(json);
    check(world.width() == 2 && world.height() == 1 && world.entities().size() == 1,
          "export loads in real world parser");
    check(world.entities()[0].profile.name == "Hello|Native", "named objects survive world import");
    check(document.undo() && document.empty(), "whole gesture undoes as one command");
    check(document.redo() && document.serialize() == json,
          "redo restores exact JM dictionary and data");
    MapDocument loaded(catalog, flash::source_root);
    loaded.load(json);
    check(!loaded.dirty() && loaded.serialize() == json, "JM roundtrip retains layers and names");
    check(loaded.tile(127, 128).name == "Hello|Native" && loaded.tile(128, 128).ids[2] == "Spawn",
          "load centers map and retains region");
    loaded.clear();
    check(loaded.empty() && loaded.undo() && loaded.serialize() == json, "clear can be undone");
    loaded.paint(127, 128, EditorLayer::object, "");
    check(loaded.tile(127, 128).name.empty(), "erasing object clears its name");
    check(!loaded.redo(), "editing invalidates redo history");
    const auto before = loaded.serialize();
    rejects([&] { loaded.load("{\"width\":257,\"height\":1}"); });
    rejects([&] { loaded.load("{\"width\":1,\"height\":1,\"dict\":[{}],\"data\":\"broken\"}"); });
    check(loaded.serialize() == before, "failed load preserves current document");
    rejects([&] { loaded.paint(-1, 0, EditorLayer::ground, ground); });
    rejects([&] { loaded.paint(256, 0, EditorLayer::ground, ground); });
    loaded.clear();
    rejects([&] { (void)loaded.serialize(); });
    std::cout << "Editor layers, undo/redo, JM roundtrip, world compatibility and validation "
                 "checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
