#include "realm/mapeditor/MapDocument.hpp"
#include "realm/map/World.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const char* reason) {
  if (!value) {
    throw std::runtime_error(reason);
  }
}

std::string folded(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

// Flash MoreStringUtil.cmp, approximated as case-insensitive then raw order.
bool id_before(const std::string& left, const std::string& right) {
  return folded(left) != folded(right) ? folded(left) < folded(right) : left < right;
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
    Catalog catalog(flash::assets_root);
    MapDocument document(catalog, flash::assets_root);
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
    check(json.find("\"width\"") < json.find("\"height\"") &&
              json.find("\"height\"") < json.find("\"dict\"") &&
              json.find("\"dict\"") < json.find("\"data\""),
          "JM top-level keys follow Flash order");
    MapDocument loaded(catalog, flash::assets_root);
    loaded.load(json);
    check(!loaded.dirty() && loaded.serialize() == json, "JM roundtrip retains layers and names");
    // A 2 by 1 map loads at x = int(128 - 1), y = int(128 - 0.5).
    check(loaded.tile(127, 127).name == "Hello|Native" && loaded.tile(128, 127).ids[2] == "Spawn",
          "load centers map and retains region");
    loaded.clear();
    check(loaded.empty() && loaded.undo() && loaded.serialize() == json, "clear can be undone");
    loaded.paint(127, 127, EditorLayer::object, "");
    check(loaded.tile(127, 127).name == "Hello|Native", "erasing object keeps its name");
    loaded.paint(127, 127, EditorLayer::object, "Sign");
    check(loaded.tile(127, 127).name == "Hello|Native", "repainting object keeps its name");
    check(!loaded.redo(), "editing invalidates redo history");
    const auto before = loaded.serialize();
    rejects([&] { loaded.load("{\"width\":257,\"height\":1}"); });
    rejects([&] { loaded.load("{\"width\":1,\"height\":1,\"dict\":[{}],\"data\":\"broken\"}"); });
    check(loaded.serialize() == before, "failed load preserves current document");
    rejects([&] { loaded.paint(-1, 0, EditorLayer::ground, ground); });
    rejects([&] { loaded.paint(256, 0, EditorLayer::ground, ground); });
    loaded.clear();
    rejects([&] { (void)loaded.serialize(); });

    // Palette: hidden items/players/projectiles, sorted grounds and objects.
    const auto& objects = document.choices(EditorLayer::object);
    std::size_t visible = 0;
    for (const auto& object : catalog.objects()) {
      visible += !(object.xml.has("Item") || object.xml.has("Player") ||
                   object.class_name == "Projectile");
    }
    check(objects.size() == visible, "object palette hides items, players and projectiles");
    for (const auto& choice : objects) {
      const auto* object = catalog.object(choice.id);
      check(object && !object->xml.has("Item") && !object->xml.has("Player") &&
                object->class_name != "Projectile",
            "object palette entries are placeable");
    }
    const auto& grounds = document.choices(EditorLayer::ground);
    for (std::size_t i = 1; i < objects.size(); ++i) {
      check(!id_before(objects[i].id, objects[i - 1].id), "object palette is sorted by id");
    }
    for (std::size_t i = 1; i < grounds.size(); ++i) {
      check(!id_before(grounds[i].id, grounds[i - 1].id), "ground palette is sorted by id");
    }

    // Names are independent of the object: they survive erase and repaint.
    MapDocument named(catalog, flash::assets_root);
    named.paint(10, 10, EditorLayer::object, objects[0].id);
    named.name(10, 10, "Kept");
    named.paint(10, 10, EditorLayer::object, "");
    check(named.tile(10, 10).name == "Kept" && named.empty(),
          "name survives object erase and is not content");
    named.paint(10, 10, EditorLayer::object, objects[0].id);
    check(named.tile(10, 10).name == "Kept", "name survives object repaint");

    // Odd sizes round the half up: a 5 by 1 map loads at x=125 (int(128 - 2.5)).
    MapDocument odd(catalog, flash::assets_root);
    odd.begin_edit();
    for (int x = 125; x < 130; ++x) {
      odd.paint(x, 128, EditorLayer::ground, ground);
    }
    odd.end_edit();
    MapDocument odd_loaded(catalog, flash::assets_root);
    odd_loaded.load(odd.serialize());
    check(odd_loaded.tile(125, 127).ids[0] == ground && odd_loaded.tile(129, 127).ids[0] == ground &&
              odd_loaded.tile(124, 127).ids[0].empty() && odd_loaded.tile(130, 127).ids[0].empty(),
          "odd width 5 loads at left 125");

    // An explicit empty name is a name: it is written and round-trips.
    MapDocument blank(catalog, flash::assets_root);
    blank.paint(20, 20, EditorLayer::object, objects[0].id);
    blank.name(20, 20, "");
    const auto blank_json = blank.serialize();
    check(blank_json.find("\"name\":\"\"") != std::string::npos, "empty object name is written");
    MapDocument blank_loaded(catalog, flash::assets_root);
    blank_loaded.load(blank_json);
    check(blank_loaded.tile(127, 127).name.has_value() &&
              blank_loaded.tile(127, 127).name->empty() &&
              blank_loaded.serialize() == blank_json,
          "loaded empty name round-trips");

    // Entries with several objects keep the last known one; unknown ids are skipped.
    MapDocument single(catalog, flash::assets_root);
    single.paint(128, 128, EditorLayer::ground, ground);
    auto entry_json = nlohmann::json::parse(single.serialize());
    const nlohmann::json first{{"id", objects[0].id}, {"name", "First"}};
    const nlohmann::json second{{"id", objects[1].id}};
    const nlohmann::json unknown{{"id", "NoSuchObjectId"}, {"name", "Lost"}};
    entry_json["dict"][0]["objs"] = nlohmann::json::array({first, second, unknown});
    MapDocument many(catalog, flash::assets_root);
    many.load(entry_json.dump());
    check(many.tile(127, 127).ids[1] == objects[1].id && !many.tile(127, 127).name &&
              many.tile(127, 127).ids[0] == ground,
          "last known object wins and unknown ids are skipped");
    entry_json["dict"][0]["objs"] = nlohmann::json::array({unknown});
    MapDocument none(catalog, flash::assets_root);
    none.load(entry_json.dump());
    check(none.tile(127, 127).ids[1].empty() && none.tile(127, 127).ids[0] == ground,
          "unknown object id is skipped");

    // Undo history is unbounded; an edit that changes nothing records nothing.
    MapDocument capped(catalog, flash::assets_root);
    for (int x = 0; x < 150; ++x) {
      capped.paint(x, 0, EditorLayer::ground, ground);
    }
    capped.begin_edit();
    capped.paint(0, 0, EditorLayer::ground, ground);
    capped.end_edit();
    for (int x = 0; x < 150; ++x) {
      check(capped.undo(), "undo history is not capped");
    }
    check(!capped.undo() && capped.empty(), "no-op edit records no undo step");

    std::cout << "Editor layers, undo/redo, JM roundtrip, world compatibility and validation "
                 "checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
