#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace realm {
// XML remains available for data-driven UI/abilities; simulation uses the dense
// typed definitions in ObjectProperties.hpp, GroundProperties.hpp and ParticleProperties.hpp.
// Parsing is a startup operation, never per frame.
class Xml {
 public:
  Xml() = default;
  explicit Xml(const void* node) : node_(node) {}
  [[nodiscard]] explicit operator bool() const { return node_ != nullptr; }
  [[nodiscard]] Xml child(std::string_view name) const;
  [[nodiscard]] std::vector<Xml> children(std::string_view name = {}) const;
  [[nodiscard]] std::string name() const;
  [[nodiscard]] std::string text() const;
  [[nodiscard]] std::string attribute(std::string_view name, std::string fallback = {}) const;
  [[nodiscard]] bool has(std::string_view name) const { return static_cast<bool>(child(name)); }
  [[nodiscard]] double number(std::string_view name, double fallback = 0) const;
  [[nodiscard]] int integer(std::string_view name, int fallback = 0) const;

 private:
  const void* node_{};
};
double number(std::string_view s, double fallback = 0);
}
