#include "realm/mapeditor/MapDocument.hpp"
#include <nlohmann/json.hpp>
#include <libxml/parser.h>
#include <zlib.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace realm {
namespace {
constexpr std::string_view alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode(std::span<const unsigned char> bytes) {
  std::string result;
  unsigned value{}, bits{};
  for (auto byte : bytes) {
    value = (value << 8U) | byte;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      result += alphabet[(value >> bits) & 63U];
    }
  }
  if (bits) {
    result += alphabet[(value << (6 - bits)) & 63U];
  }
  while (result.size() % 4) {
    result += '=';
  }
  return result;
}

std::vector<unsigned char> decode(std::string_view source) {
  std::vector<unsigned char> result;
  unsigned value{}, bits{};
  bool padded{};
  for (const char c : source) {
    if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
      continue;
    }
    if (c == '=') {
      padded = true;
      continue;
    }
    const auto at = alphabet.find(c);
    if (padded || at == std::string_view::npos) {
      throw std::runtime_error("Invalid map base64");
    }
    value = (value << 6U) | static_cast<unsigned>(at);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      result.push_back(static_cast<unsigned char>(value >> bits));
    }
  }
  return result;
}

void coordinates(int x, int y) {
  if (x < 0 || y < 0 || x >= MapDocument::extent || y >= MapDocument::extent) {
    throw std::out_of_range("Map editor tile is outside the 256 by 256 canvas");
  }
}
}

MapDocument::MapDocument(const Catalog& catalog, const std::filesystem::path& root) {
  for (const auto& ground : catalog.grounds()) {
    choices_[0].push_back({ground.id, ground.type, 0});
  }
  for (const auto& object : catalog.objects()) {
    choices_[1].push_back({object.id, object.type, 0});
  }
  const auto path = root / "src/kabam/rotmg/assets/EmbeddedData_RegionsCXML.dat";
  const auto document = std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>(
      xmlReadFile(path.c_str(), nullptr, XML_PARSE_NONET), xmlFreeDoc);
  if (!document) {
    throw std::runtime_error("Cannot load map editor regions");
  }
  for (const auto region : Xml(xmlDocGetRootElement(document.get())).children("Region")) {
    choices_[2].push_back({region.attribute("id"),
                           static_cast<int>(number(region.attribute("type"))),
                           static_cast<std::uint32_t>(region.number("Color"))});
  }
}

const EditorTile& MapDocument::tile(int x, int y) const {
  coordinates(x, y);
  static const EditorTile blank;
  const auto found = tiles_.find(x + y * extent);
  return found == tiles_.end() ? blank : found->second;
}

const std::vector<EditorChoice>& MapDocument::choices(EditorLayer layer) const {
  return choices_.at(static_cast<std::size_t>(layer));
}

void MapDocument::begin_edit() {
  if (editing_) {
    return;
  }
  before_.clear();
  editing_ = true;
}

void MapDocument::store(int index, EditorTile tile) {
  if (editing_ && !before_.contains(index)) {
    const auto found = tiles_.find(index);
    before_[index] = found == tiles_.end() ? EditorTile{} : found->second;
  }
  if (tile.empty()) {
    tiles_.erase(index);
  } else {
    tiles_[index] = std::move(tile);
  }
  ++revision_;
}

void MapDocument::paint(int x, int y, EditorLayer layer, std::string id) {
  auto value = tile(x, y);
  const auto index = static_cast<std::size_t>(layer);
  if (value.ids.at(index) == id) {
    return;
  }
  const bool single = !editing_;
  begin_edit();
  value.ids[index] = std::move(id);
  if (layer == EditorLayer::object) {
    value.name.clear();
  }
  store(x + y * extent, std::move(value));
  if (single) {
    end_edit();
  }
}

void MapDocument::name(int x, int y, std::string text) {
  auto value = tile(x, y);
  if (value.ids[1].empty() || value.name == text) {
    return;
  }
  const bool single = !editing_;
  begin_edit();
  value.name = std::move(text);
  store(x + y * extent, std::move(value));
  if (single) {
    end_edit();
  }
}

void MapDocument::end_edit() {
  if (!editing_) {
    return;
  }
  editing_ = false;
  Change change;
  for (const auto& [index, before] : before_) {
    const auto found = tiles_.find(index);
    const auto after = found == tiles_.end() ? EditorTile{} : found->second;
    if (before == after) {
      continue;
    }
    change.before[index] = before;
    change.after[index] = after;
  }
  if (!change.before.empty()) {
    undo_.push_back(std::move(change));
    if (undo_.size() > 100) {
      undo_.erase(undo_.begin());
    }
    redo_.clear();
  }
  before_.clear();
}

bool MapDocument::undo() {
  end_edit();
  if (undo_.empty()) {
    return false;
  }
  auto change = std::move(undo_.back());
  undo_.pop_back();
  for (const auto& [index, tile] : change.before) {
    store(index, tile);
  }
  redo_.push_back(std::move(change));
  ++revision_;
  return true;
}

bool MapDocument::redo() {
  end_edit();
  if (redo_.empty()) {
    return false;
  }
  auto change = std::move(redo_.back());
  redo_.pop_back();
  for (const auto& [index, tile] : change.after) {
    store(index, tile);
  }
  undo_.push_back(std::move(change));
  ++revision_;
  return true;
}

void MapDocument::clear() {
  end_edit();
  begin_edit();
  while (!tiles_.empty()) {
    store(tiles_.begin()->first, {});
  }
  end_edit();
}

std::optional<Rect> MapDocument::bounds() const {
  if (tiles_.empty()) {
    return {};
  }
  int left = extent, top = extent, right{}, bottom{};
  for (const auto& [index, value] : tiles_) {
    (void)value;
    left = std::min(left, index % extent);
    right = std::max(right, index % extent);
    top = std::min(top, index / extent);
    bottom = std::max(bottom, index / extent);
  }
  return Rect{static_cast<double>(left), static_cast<double>(top),
              static_cast<double>(right - left + 1), static_cast<double>(bottom - top + 1)};
}

std::string MapDocument::serialize() const {
  const auto area = bounds();
  if (!area) {
    throw std::runtime_error("Draw at least one tile before saving or testing the map");
  }
  using Json = nlohmann::json;
  auto dictionary = Json::array();
  std::map<std::string, unsigned> indices;
  std::vector<unsigned char> raw;
  for (int y = static_cast<int>(area->y); y < area->y + area->height; ++y) {
    for (int x = static_cast<int>(area->x); x < area->x + area->width; ++x) {
      const auto& value = tile(x, y);
      Json entry = Json::object();
      if (!value.ids[0].empty()) {
        entry["ground"] = value.ids[0];
      }
      if (!value.ids[1].empty()) {
        Json object{{"id", value.ids[1]}};
        if (!value.name.empty()) {
          object["name"] = value.name;
        }
        entry["objs"] = Json::array({object});
      }
      if (!value.ids[2].empty()) {
        entry["regions"] = Json::array({Json{{"id", value.ids[2]}}});
      }
      auto [found, added] =
          indices.try_emplace(entry.dump(), static_cast<unsigned>(dictionary.size()));
      if (added) {
        // Flash reads these indices with readShort(), so the dictionary must stay in the signed
        // 16-bit range.
        if (dictionary.size() >= 32768) {
          throw std::runtime_error("Map has too many distinct tiles for Flash JM format");
        }
        dictionary.push_back(entry);
      }
      raw.push_back(static_cast<unsigned char>(found->second >> 8U));
      raw.push_back(static_cast<unsigned char>(found->second & 255U));
    }
  }
  uLongf length = compressBound(static_cast<uLong>(raw.size()));
  std::vector<unsigned char> compressed(length);
  if (compress2(compressed.data(), &length, raw.data(), static_cast<uLong>(raw.size()),
                Z_DEFAULT_COMPRESSION) != Z_OK) {
    throw std::runtime_error("Cannot compress map");
  }
  compressed.resize(length);
  return Json{{"width", static_cast<int>(area->width)},
              {"height", static_cast<int>(area->height)},
              {"dict", dictionary},
              {"data", encode(compressed)}}
      .dump();
}

void MapDocument::load(std::string_view json) {
  if (json.size() > 16 * 1024 * 1024) {
    throw std::runtime_error("Map file exceeds 16 MiB");
  }
  const auto source = nlohmann::json::parse(json);
  const int width = source.at("width").get<int>(), height = source.at("height").get<int>();
  if (width <= 0 || height <= 0 || width > extent || height > extent) {
    throw std::runtime_error("Map exceeds the 256 by 256 editor canvas");
  }
  const auto& dict = source.at("dict");
  if (!dict.is_array() || dict.empty() || dict.size() > 32768) {
    throw std::runtime_error("Invalid map dictionary");
  }
  const auto compressed = decode(source.at("data").get<std::string>());
  std::vector<unsigned char> raw(static_cast<std::size_t>(width * height) * 2);
  uLongf length = raw.size();
  uLong consumed = compressed.size();
  if (uncompress2(raw.data(), &length, compressed.data(), &consumed) != Z_OK ||
      length != raw.size() || consumed != compressed.size()) {
    throw std::runtime_error("Invalid map data length");
  }
  Tiles loaded;
  const int left = extent / 2 - width / 2, top = extent / 2 - height / 2;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto at = static_cast<std::size_t>(y * width + x) * 2;
      const auto index =
          static_cast<std::size_t>((static_cast<unsigned>(raw[at]) << 8U) | raw[at + 1]);
      const auto& entry = dict.at(index);
      if (!entry.is_object()) {
        throw std::runtime_error("Invalid map dictionary tile");
      }
      EditorTile tile;
      tile.ids[0] = entry.value("ground", std::string{});
      for (const auto& obj : entry.value("objs", nlohmann::json::array())) {
        if (!tile.ids[1].empty()) {
          throw std::runtime_error("Editor supports one object per tile");
        }
        tile.ids[1] = obj.at("id").get<std::string>();
        tile.name = obj.value("name", std::string{});
      }
      for (const auto& region : entry.value("regions", nlohmann::json::array())) {
        if (!tile.ids[2].empty()) {
          throw std::runtime_error("Editor supports one region per tile");
        }
        tile.ids[2] = region.at("id").get<std::string>();
      }
      if (!tile.empty()) {
        loaded[x + left + (y + top) * extent] = std::move(tile);
      }
    }
  }
  tiles_ = std::move(loaded);
  saved_ = tiles_;
  before_.clear();
  undo_.clear();
  redo_.clear();
  editing_ = false;
  ++revision_;
}

void MapDocument::load_file(const std::filesystem::path& path) {
  if (std::filesystem::file_size(path) > 16 * 1024 * 1024) {
    throw std::runtime_error("Map file exceeds 16 MiB");
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Cannot open map: " + path.string());
  }
  load(std::string{std::istreambuf_iterator<char>(file), {}});
}

void MapDocument::save_file(const std::filesystem::path& path) {
  const auto json = serialize();
  const auto temp = path.string() + ".tmp";
  {
    std::ofstream file(temp, std::ios::binary);
    if (!file || !(file << json) || !file.flush()) {
      throw std::runtime_error("Cannot save map: " + path.string());
    }
  }
  std::filesystem::rename(temp, path);
  saved_ = tiles_;
}
}
