#pragma once
// Flash: com/company/assembleegameclient/objects/ObjectLibrary.as
#include "realm/map/GroundProperties.hpp"
#include "realm/objects/ObjectProperties.hpp"
#include "realm/objects/particles/ParticleProperties.hpp"
#include "realm/util/Xml.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace realm {
class Catalog {
 public:
  explicit Catalog(const std::filesystem::path& flash_root);
  ~Catalog();
  Catalog(const Catalog&) = delete;
  Catalog& operator=(const Catalog&) = delete;
  [[nodiscard]] const ObjectDef* object(std::uint16_t type) const;
  [[nodiscard]] const GroundDef* ground(std::uint16_t type) const;
  [[nodiscard]] const ObjectDef* object(std::string_view id) const;
  [[nodiscard]] const GroundDef* ground(std::string_view id) const;

  [[nodiscard]] const std::vector<ObjectDef>& objects() const { return objects_; }

  // ObjectLibrary.hexTransforms_: object types in Group "Hexable", in load order; a Hexed player
  // becomes one of them.
  [[nodiscard]] const std::vector<std::uint16_t>& hex_transforms() const { return hex_transforms_; }

  [[nodiscard]] const std::vector<GroundDef>& grounds() const { return grounds_; }

  [[nodiscard]] const std::vector<std::string>& unresolved_textures() const { return unresolved_; }

  [[nodiscard]] TextureDef texture(Xml node);
  [[nodiscard]] const ParticleDef* particle(std::string_view id) const;

  [[nodiscard]] const std::vector<SkinDef>& skins() const { return skins_; }

  [[nodiscard]] const SkinDef* skin(int id, int class_type) const;
  [[nodiscard]] const TextureDef& player_texture(int class_type, int skin_id) const;
  [[nodiscard]] int portrait_type(int class_type, int skin_id) const;

 private:
  struct Documents;
  std::unique_ptr<Documents> documents_;
  std::vector<ObjectDef> objects_;
  std::vector<std::uint16_t> hex_transforms_;
  std::vector<SkinDef> skins_;
  std::vector<ParticleDef> particles_;
  std::vector<GroundDef> grounds_;
  std::vector<std::int32_t> object_lookup_;
  std::vector<std::int32_t> ground_lookup_;
  std::unordered_map<std::string, std::uint16_t> object_names_;
  std::unordered_map<std::string, std::uint16_t> ground_names_;
  std::vector<std::string> unresolved_;
};
}
