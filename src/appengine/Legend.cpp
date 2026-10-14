#include "realm/appengine/Legend.hpp"
#include "realm/appengine/SavedCharactersList.hpp"
#include "AppEngineXml.hpp"
#include <stdexcept>
#include <cerrno>
#include <limits>
#include <set>
#include <utility>
namespace realm {
using namespace appengine_xml;
std::vector<Legend> parse_legends(std::string_view source, std::string_view timespan) {
  if (auto error = app_error(source)) throw std::runtime_error(*error);
  const auto doc = parse(source);
  const auto* root = xmlDocGetRootElement(doc->doc);
  if (!root || name(root) != "FameList") throw std::runtime_error("Legends reply has no <FameList> root");
  if (attribute(root, "timespan") != timespan) throw std::runtime_error("Legends reply has the wrong timespan");
  const auto required_int = [](const std::string& value) {
    char* end{};
    errno = 0;
    const auto result = std::strtoll(value.c_str(), &end, 0);
    if (value.empty() || end == value.c_str() || *end || errno == ERANGE || result < std::numeric_limits<int>::min() ||
        result > std::numeric_limits<int>::max())
      throw std::runtime_error("Invalid Legends numeric field");
    return static_cast<int>(result);
  };
  const auto slots = [&](const xmlNode* node, std::array<int, 4>& out) {
    if (!node) return;
    const auto value = text(node);
    std::size_t at{};
    // Only four equipped slots are displayed; the server also sends inventory.
    for (std::size_t i = 0; i < 4 && at < value.size(); ++i) {
      const auto end = value.find(',', at);
      out[i] = required_int(value.substr(at, end == std::string::npos ? end : end - at));
      if (end == std::string::npos) break;
      at = end + 1;
    }
  };
  std::vector<Legend> result;
  std::set<std::pair<int, int>> seen;
  std::size_t count{};
  for (auto* c = root->children; c; c = c->next)
    if (c->type == XML_ELEMENT_NODE && name(c) == "FameListElem") {
      if (++count > 256) throw std::runtime_error("Legends list exceeds limit");
      Legend legend;
      legend.account_id = required_int(attribute(c, "accountId"));
      legend.character_id = required_int(attribute(c, "charId"));
      if (!seen.emplace(legend.account_id, legend.character_id).second) continue;
      legend.name = text(child(c, "Name"));
      if (legend.name.size() > 128) throw std::runtime_error("Legends name exceeds limit");
      legend.total_fame = required_int(text(child(c, "TotalFame")));
      legend.character.id = legend.character_id;
      legend.character.object_type = required_int(text(child(c, "ObjectType")));
      if (legend.character.object_type < 1 || legend.character.object_type > 65535 || legend.total_fame < 0)
        throw std::runtime_error("Invalid Legends character or fame");
      legend.character.skin = integer(child(c, "Texture"));
      legend.character.tex1 = integer(child(c, "Tex1"));
      legend.character.tex2 = integer(child(c, "Tex2"));
      slots(child(c, "Equipment"), legend.equipment);
      slots(child(c, "ItemDatas"), legend.item_data);
      result.push_back(std::move(legend));
    }
  return result;
}
}
