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
struct SkinDef {
  int id{}, class_type{};
  std::string name;
  TextureDef texture;
};
}
