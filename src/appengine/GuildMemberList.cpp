#include "realm/appengine/GuildMemberList.hpp"
#include "realm/appengine/SavedCharactersList.hpp"
#include "AppEngineXml.hpp"
#include <stdexcept>

namespace realm {
using namespace appengine_xml;

GuildMemberList parse_guild_members(std::string_view source) {
  if (auto error = app_error(source)) {
    throw std::runtime_error(*error);
  }

  const auto doc = parse(source);
  const auto* root = xmlDocGetRootElement(doc->doc);
  if (!root || name(root) != "Members") {
    throw std::runtime_error("Guild list reply has no <Members> root");
  }

  GuildMemberList list;
  list.name = attribute(root, "name");
  list.fame = integer(child(root, "CurrentFame"));
  list.level = integer(child(root, "GuildLevel"));
  for (auto* node = root->children; node; node = node->next) {
    if (node->type == XML_ELEMENT_NODE && name(node) == "Member") {
      if (list.members.size() >= 1000) {
        throw std::runtime_error("Guild member list exceeds limit");
      }
      list.members.push_back(
          {text(child(node, "Name")), integer(child(node, "Rank")), integer(child(node, "Fame"))});
    }
  }
  return list;
}
}
