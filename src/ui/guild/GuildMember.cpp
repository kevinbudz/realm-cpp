#include "realm/ui/guild/GuildMember.hpp"
#include "realm/appengine/SavedCharactersList.hpp"
#include "../../appengine/AppEngineXml.hpp"
#include <stdexcept>
namespace realm {
using namespace appengine_xml;
GuildMemberList parse_guild_members(std::string_view source) {
  if (auto error = app_error(source)) throw std::runtime_error(*error);
  const auto doc = parse(source);
  const auto* root = xmlDocGetRootElement(doc->doc);
  if (!root || name(root) != "Members") throw std::runtime_error("Guild list reply has no <Members> root");
  GuildMemberList list;
  list.name = attribute(root, "name");
  list.fame = integer(child(root, "CurrentFame"));
  list.level = integer(child(root, "GuildLevel"));
  for (auto* c = root->children; c; c = c->next)
    if (c->type == XML_ELEMENT_NODE && name(c) == "Member") {
      if (list.members.size() >= 1000) throw std::runtime_error("Guild member list exceeds limit");
      list.members.push_back({text(child(c, "Name")), integer(child(c, "Rank")), integer(child(c, "Fame"))});
    }
  return list;
}
}
