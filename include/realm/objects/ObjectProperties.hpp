#pragma once
#include "realm/objects/ProjectileProperties.hpp"
#include "realm/util/Xml.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace realm {
struct EffectDef {
  std::string kind;
  std::string particle;
  std::uint32_t color{};
  double cooldown{};
  double rate{5};
  double speed{};
  double speed_variance{.5};
  double spread{};
  double life{1};
  double life_variance{};
  double rise{3};
  double rise_variance{};
  double rise_acc{};
  double z_offset{};
  int size{3};
  int range_x{};
  int range_y{};
  int bitmap_sheet{-1};
  int bitmap_index{};
};

struct TextureDef {
  EffectDef effect;
  int sheet{-1};
  int animated{-1};
  int index{};
  int mask_sheet{-1};
  int mask_index{};
  std::vector<TextureDef> random;
  std::unordered_map<int, TextureDef> alternate;
  [[nodiscard]] const TextureDef& select(std::uint32_t id) const;
};

struct ProjectileDef {
  int id{};
  int size{-1};
  int min_damage{};
  int max_damage{};
  std::string object_id;
  ProjectileProperties motion;
  bool multi_hit{};
  bool passes_cover{};
  bool armor_piercing{};
  bool particle_trail{};
  Xml xml;
};

struct ObjectDef {
  std::uint16_t type{};
  std::string id;
  std::string display_id;
  std::string class_name;
  std::string model;
  TextureDef texture;
  TextureDef top;
  // ConnectedWall's per-pattern cap art: dot, short line, L, line, T and cross. These XML nodes
  // contain bare File/Index children rather than the usual Texture wrapper.
  std::array<TextureDef, 6> connected_textures;
  int shadow_size{100};
  int size{100};
  int min_size{100};
  int max_size{100};
  int size_step{5};
  int max_hp{};
  int defense{};
  int slot_type{-1};
  int tier{-1};
  int max_hp_cap{};
  int max_mp{};
  int max_mp_cap{};
  // HUD order: attack, defense, speed, dexterity, vitality, wisdom.
  std::array<int, 6> stat_caps{};
  int num_projectiles{1};
  int mp_cost{};
  int cooldown_ms{200};
  double rate_of_fire{};
  double arc_gap{};
  bool usable{};
  bool consumable{};
  bool inventory_use{};
  bool ability_target{};
  bool ability_teleport{};
  bool ability_shoot{};
  double z{};
  double rotation{};
  double angle_correction{};
  double blood_probability{};
  // ConnectedWall's local extrusion dimensions, copied from ConnectedWall.as.
  double wall_base_indent{0.5};
  double wall_top_indent{0.25};
  double wall_height{1};
  std::uint32_t color{0xffffff};
  std::uint32_t blood_color{0xff0000};
  std::uint32_t shadow_color{};
  bool player{};
  bool enemy{};
  bool draw_on_ground{};
  bool draw_under{};
  bool occupy{};
  bool full_occupy{};
  bool enemy_occupy{};
  bool stationary{};
  bool no_minimap{};
  bool protect_ground{};
  bool protect_sink{};
  bool flying{};
  bool show_name{};
  // Portal.nexusPortal_: the server-named portal label (e.g. "Medusa (12/85)") is always drawn.
  bool nexus_portal{};
  bool dont_face_attacks{};
  bool hexable{};
  // Classes implementing IInteractiveObject, and the Container subclasses among them.
  bool interactive{};
  bool container{};
  bool wall_texture_repeat{true};
  bool top_texture_repeat{true};
  // Sounds (Flash names, see SoundCues): HitSound and DeathSound exist for Character and Player
  // classes only, with Character.as defaults when the XML omits them; OldSound is a weapon's
  // first-shot sound; <Sound> tags are preloaded with the object; Loot marks a container that plays
  // loot_appears when it appears near the player.
  std::string hit_sound;
  std::string death_sound;
  std::string old_sound;
  std::vector<std::string> sounds;
  bool loot{};
  std::vector<int> slot_types;
  std::vector<ProjectileDef> projectiles;
  // Default dye colors (Tex1/Tex2), decoded once at load: World::add copies them into every new
  // entity.
  std::uint32_t tex1{};
  std::uint32_t tex2{};
  Xml xml;
};

struct SkinDef {
  int id{};
  int class_type{};
  std::string name;
  TextureDef texture;
};
}
