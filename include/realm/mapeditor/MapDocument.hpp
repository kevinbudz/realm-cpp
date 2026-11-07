#pragma once
// Flash: com/company/assembleegameclient/mapeditor/MEMap.as and EditingScreen.as
#include "realm/objects/Catalog.hpp"
#include <array>
#include <map>
#include <optional>
#include <span>
#include <unordered_set>

namespace realm {
enum class EditorLayer { ground, object, region };

struct EditorTile {
  std::array<std::string, 3> ids;
  // Flash METile.objName_: kept when the object is erased or repainted, and only written to JM
  // while an object is present.
  std::optional<std::string> name;
  bool operator==(const EditorTile&) const = default;

  [[nodiscard]] bool has_content() const {
    return !ids[0].empty() || !ids[1].empty() || !ids[2].empty();
  }

  [[nodiscard]] bool empty() const { return !has_content() && !name; }
};

struct EditorChoice {
  std::string id;
  int type{};
  std::uint32_t color{};
};

class MapDocument {
 public:
  static constexpr int extent = 256;
  MapDocument(const Catalog& catalog, const std::filesystem::path& flash_root);
  [[nodiscard]] const EditorTile& tile(int x, int y) const;
  [[nodiscard]] const std::vector<EditorChoice>& choices(EditorLayer layer) const;
  // A gesture is one undo operation, including a drag or rectangle of tiles.
  void begin_edit();
  void paint(int x, int y, EditorLayer layer, std::string id);
  void name(int x, int y, std::optional<std::string> value);
  void end_edit();
  bool undo();
  bool redo();
  void clear();

  // True when no tile has a ground, object or region; name-only tiles do not count.
  [[nodiscard]] bool empty() const;

  [[nodiscard]] std::optional<Rect> bounds() const;
  // Flash .jm dictionary of IDs with big-endian uint16 indices, zlib-compressed and base64-encoded.
  [[nodiscard]] std::string serialize() const;
  void load(std::string_view json);
  void load_file(const std::filesystem::path& path);
  void save_file(const std::filesystem::path& path);

  [[nodiscard]] std::uint64_t revision() const { return revision_; }

  [[nodiscard]] bool dirty() const { return tiles_ != saved_; }

 private:
  using Tiles = std::map<int, EditorTile>;

  struct Change {
    Tiles before, after;
  };

  void store(int index, EditorTile tile);
  Tiles tiles_, saved_, before_;
  std::vector<Change> undo_, redo_;
  std::array<std::vector<EditorChoice>, 3> choices_;
  // Every object in the catalog, including items and players that the palette hides.
  std::unordered_set<std::string> known_objects_;
  bool editing_{};
  std::uint64_t revision_{};
};
}
