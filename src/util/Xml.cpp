#include "realm/util/Xml.hpp"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdexcept>

namespace realm {
namespace {
const xmlNode* node(const void* p) { return static_cast<const xmlNode*>(p); }
bool named(const xmlNode* p, std::string_view name) {
  return p && p->type == XML_ELEMENT_NODE && name == reinterpret_cast<const char*>(p->name);
}
}
double number(std::string_view s, double fallback) {
  if (s.empty()) return fallback;
  std::string value(s);
  try {
    return (s.starts_with("0x") || s.starts_with("0X")) ? static_cast<double>(std::stoll(value, nullptr, 16))
                                                        : std::stod(value);
  } catch (const std::exception&) {
    throw std::runtime_error("Invalid definition number: " + value);
  }
}
Xml Xml::child(std::string_view name) const {
  if (node_)
    for (auto p = node(node_)->children; p; p = p->next)
      if (named(p, name)) return Xml(p);
  return {};
}
std::vector<Xml> Xml::children(std::string_view name) const {
  std::vector<Xml> result;
  if (node_)
    for (auto p = node(node_)->children; p; p = p->next)
      if (p->type == XML_ELEMENT_NODE && (name.empty() || named(p, name))) result.emplace_back(p);
  return result;
}
std::string Xml::name() const { return node_ ? reinterpret_cast<const char*>(node(node_)->name) : ""; }
std::string Xml::text() const {
  if (!node_) return {};
  auto p = xmlNodeGetContent(node(node_));
  if (!p) return {};
  std::string s(reinterpret_cast<const char*>(p));
  xmlFree(p);
  const auto first = s.find_first_not_of(" \r\n\t"), last = s.find_last_not_of(" \r\n\t");
  return first == std::string::npos ? "" : s.substr(first, last - first + 1);
}
std::string Xml::attribute(std::string_view name, std::string fallback) const {
  if (!node_) return fallback;
  const std::string key(name);
  auto p = xmlGetProp(node(node_), reinterpret_cast<const xmlChar*>(key.c_str()));
  if (!p) return fallback;
  std::string s(reinterpret_cast<const char*>(p));
  xmlFree(p);
  return s;
}
double Xml::number(std::string_view name, double fallback) const { return realm::number(child(name).text(), fallback); }
int Xml::integer(std::string_view name, int fallback) const { return static_cast<int>(number(name, fallback)); }
}
