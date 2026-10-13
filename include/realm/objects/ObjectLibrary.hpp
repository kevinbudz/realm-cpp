#pragma once
#include "realm/objects/ProjectileProperties.hpp"
#include <filesystem>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace realm {
// XML remains available for data-driven UI/abilities; simulation uses the dense
// typed definitions below. Parsing is a startup operation, never per frame.
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

struct EffectDef {
  std::string kind, particle;
  std::uint32_t color{};
  double cooldown{}, rate{5}, speed{}, speed_variance{.5}, spread{}, life{1}, life_variance{}, rise{3}, rise_variance{},
      rise_acc{}, z_offset{};
  int size{3}, range_x{}, range_y{}, bitmap_sheet{-1}, bitmap_index{};
};
struct TextureDef {
  EffectDef effect;
  int sheet{-1}, animated{-1}, index{}, mask_sheet{-1}, mask_index{};
  std::vector<TextureDef> random;
  std::unordered_map<int, TextureDef> alternate;
  [[nodiscard]] const TextureDef& select(std::uint32_t id) const;
};
struct GroundDef {
  std::uint16_t type{};
  std::string id;
  TextureDef texture, edge, corner, inner_corner, top;
  double speed{1}, offset_x{}, offset_y{}, animate_x{}, animate_y{}, top_animate_x{}, top_animate_y{};
  int damage{}, blend_priority{-1}, composite_priority{}, animate{}, top_animate{};
  bool no_walk{true}, push{}, sink{}, sinking{}, random_offset{}, same_type_edge{};
  Xml xml;
};
struct ProjectileDef {
  int id{}, size{-1}, min_damage{}, max_damage{};
  std::string object_id;
  ProjectileProperties motion;
  bool multi_hit{}, passes_cover{}, armor_piercing{}, particle_trail{};
  Xml xml;
};
struct ObjectDef {
  std::uint16_t type{};
  std::string id, display_id, class_name, model;
  TextureDef texture, top;
  int shadow_size{100}, size{100}, min_size{100}, max_size{100}, size_step{5}, max_hp{}, defense{}, slot_type{-1},
      tier{-1};
  int max_hp_cap{}, max_mp{}, max_mp_cap{};
  // HUD order: attack, defense, speed, dexterity, vitality, wisdom.
  std::array<int, 6> stat_caps{};
  int num_projectiles{1}, mp_cost{}, cooldown_ms{200};
  double rate_of_fire{}, arc_gap{};
  bool usable{}, consumable{}, inventory_use{}, ability_target{}, ability_teleport{}, ability_shoot{};
  double z{}, rotation{}, angle_correction{}, blood_probability{};
  std::uint32_t color{0xffffff}, blood_color{0xff0000}, shadow_color{};
  bool player{}, enemy{}, draw_on_ground{}, draw_under{}, occupy{}, full_occupy{}, enemy_occupy{}, stationary{},
      no_minimap{}, protect_ground{}, protect_sink{}, flying{}, show_name{}, dont_face_attacks{}, hexable{};
  // Classes implementing IInteractiveObject, and the Container subclasses among them.
  bool interactive{}, container{};
  // Sounds (Flash names, see SoundCues): HitSound and DeathSound exist for Character and Player classes only, with
  // Character.as defaults when the XML omits them; OldSound is a weapon's first-shot sound; <Sound> tags are preloaded
  // with the object; Loot marks a container that plays loot_appears when it appears near the player.
  std::string hit_sound, death_sound, old_sound;
  std::vector<std::string> sounds;
  bool loot{};
  std::vector<int> slot_types;
  std::vector<ProjectileDef> projectiles;
  // Default dye colors (Tex1/Tex2), decoded once at load: World::add copies them into every new entity.
  std::uint32_t tex1{}, tex2{};
  Xml xml;
};
struct ParticleFrameDef {
  TextureDef texture;
  double duration_ms{};
};
struct ParticleAnimationDef {
  double probability{1};
  std::vector<ParticleFrameDef> frames;
};
struct ParticleDef {
  std::string id;
  TextureDef texture;
  int size{100};
  double z{}, duration_ms{};
  std::vector<ParticleAnimationDef> animations;
};
struct SkinDef {
  int id{}, class_type{};
  std::string name;
  TextureDef texture;
};
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
  // ObjectLibrary.hexTransforms_: object types in Group "Hexable", in load order; a Hexed player becomes one of them.
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
  std::vector<std::int32_t> object_lookup_, ground_lookup_;
  std::unordered_map<std::string, std::uint16_t> object_names_, ground_names_;
  std::vector<std::string> unresolved_;
};
}
