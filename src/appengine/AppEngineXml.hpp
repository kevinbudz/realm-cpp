#pragma once
// AppEngine XML reply helpers shared by the account, fame, legends and guild parsers (realm_game
// only).
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace realm::appengine_xml {
struct Document {
  xmlDoc* doc;

  ~Document() { xmlFreeDoc(doc); }
};

inline std::string_view name(const xmlNode* node) {
  return reinterpret_cast<const char*>(node->name);
}

inline const xmlNode* child(const xmlNode* node, std::string_view tag) {
  for (auto* entry = node ? node->children : nullptr; entry; entry = entry->next) {
    if (entry->type == XML_ELEMENT_NODE && name(entry) == tag) {
      return entry;
    }
  }
  return nullptr;
}

inline std::string text(const xmlNode* node) {
  if (!node) {
    return {};
  }
  auto* content = xmlNodeGetContent(node);
  std::string result = content ? reinterpret_cast<const char*>(content) : "";
  xmlFree(content);
  return result;
}

inline std::string attribute(const xmlNode* node, const char* key) {
  auto* property = xmlGetProp(node, reinterpret_cast<const xmlChar*>(key));
  std::string result = property ? reinterpret_cast<const char*>(property) : "";
  xmlFree(property);
  return result;
}

// AS3 int(String): unparsable values become zero.
inline int integer(const std::string& value) {
  char* end{};
  const auto result = std::strtol(value.c_str(), &end, 0);
  return end == value.c_str() ? 0 : static_cast<int>(result);
}

inline int integer(const xmlNode* node) {
  return integer(text(node));
}

inline std::unique_ptr<Document> parse(std::string_view source) {
  if (source.size() > 8 * 1024 * 1024) {
    throw std::runtime_error("AppEngine XML exceeds limit");
  }
  auto* doc =
      xmlReadMemory(source.data(), static_cast<int>(source.size()), nullptr, nullptr,
                    XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
  if (!doc) {
    throw std::runtime_error("Malformed AppEngine XML");
  }
  return std::unique_ptr<Document>(new Document{doc});
}
}
