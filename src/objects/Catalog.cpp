#include "realm/objects/Catalog.hpp"
#include "flash_assets.hpp"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace realm {
namespace {
// Remaining com.company.assembleegameclient.objects classes that implement
// IInteractiveObject directly or through SellableObject.
constexpr std::array<std::string_view, 12> interactive_class_names{
    "Portal",         "GuildHallPortal", "SellableObject", "ClosedVaultChest",
    "Merchant",       "GuildMerchant",   "NameChanger",    "ReskinVendor",
    "GuildChronicle", "GuildRegister",   "GuildBoard",     "CharacterChanger"};

bool interactive_class(std::string_view name) {
  return std::find(interactive_class_names.begin(), interactive_class_names.end(), name) !=
         interactive_class_names.end();
}

int sheet(std::string_view name, bool animated) {
  if (animated) {
    for (std::size_t i = 0; i < flash::animated.size(); ++i) {
      if (flash::animated[i].name == name) {
        return static_cast<int>(i);
      }
    }
  } else {
    for (std::size_t i = 0; i < flash::sheets.size(); ++i) {
      if (flash::sheets[i].name == name) {
        return static_cast<int>(i);
      }
    }
  }
  return -1;
}

std::vector<int> integers(std::string text) {
  std::replace(text.begin(), text.end(), ',', ' ');
  std::istringstream stream(text);
  std::vector<int> values;
  int value{};
  while (stream >> value) {
    values.push_back(value);
  }
  return values;
}

int animate(Xml xml) {
  const auto text = xml.text();
  return text == "Wave" ? 1 : text == "Flow" ? 2 : 0;
}
}

struct Catalog::Documents {
  std::vector<xmlDocPtr> docs;

  ~Documents() {
    for (auto* doc : docs) {
      xmlFreeDoc(doc);
    }
  }
};

Catalog::~Catalog() = default;

TextureDef Catalog::texture(Xml xml) {
  TextureDef result;
  if (!xml) {
    return result;
  }
  const auto kind = xml.name();
  // ConnectedWall's DotTexture/ShortLineTexture/LTexture/LineTexture/TTexture/CrossTexture
  // elements use a bare <File>/<Index> pair, unlike the normal <Texture> wrapper.
  if (kind != "Texture" && kind != "AnimatedTexture" && kind != "Mask" && xml.has("File")) {
    const auto file = xml.child("File").text();
    result.sheet = sheet(file, false);
    result.index = xml.integer("Index");
    if (result.sheet < 0 &&
        std::find(unresolved_.begin(), unresolved_.end(), file) == unresolved_.end()) {
      unresolved_.push_back(file);
    }
    return result;
  }
  if (kind == "Texture" || kind == "AnimatedTexture" || kind == "Mask") {
    const auto file = xml.child("File").text();
    result.index = xml.integer("Index");
    if (kind == "AnimatedTexture") {
      result.animated = sheet(file, true);
    } else {
      result.sheet = sheet(file, false);
    }
    if (result.sheet < 0 && result.animated < 0 &&
        std::find(unresolved_.begin(), unresolved_.end(), file) == unresolved_.end()) {
      unresolved_.push_back(file);
    }
    return result;
  }
  if (kind == "RandomTexture") {
    for (auto child : xml.children()) {
      if (child.name() == "Texture" || child.name() == "AnimatedTexture" ||
          child.name() == "RandomTexture") {
        result.random.push_back(texture(child));
      }
    }
    return result;
  }
  if (xml.has("Texture")) {
    result = texture(xml.child("Texture"));
  } else if (xml.has("AnimatedTexture")) {
    result = texture(xml.child("AnimatedTexture"));
  } else if (xml.has("RandomTexture")) {
    result = texture(xml.child("RandomTexture"));
  }
  if (xml.has("Mask")) {
    const auto mask = texture(xml.child("Mask"));
    result.mask_sheet = mask.sheet;
    result.mask_index = mask.index;
  }
  if (const auto effect = xml.child("Effect")) {
    auto& effect_def = result.effect;
    effect_def.kind = effect.text();
    effect_def.particle = effect.attribute("particle");
    const auto attr = [&](std::string_view key, double fallback) {
      const double value = realm::number(effect.attribute(key));
      return value == 0 ? fallback : value;
    };
    effect_def.color = static_cast<std::uint32_t>(attr("color", 0));
    effect_def.cooldown = attr("cooldown", 0);
    effect_def.rate = attr("rate", 5);
    effect_def.speed = attr("speed", 0);
    effect_def.speed_variance = attr("speedVariance", .5);
    effect_def.spread = attr("spread", 0);
    effect_def.life = attr("life", 1);
    effect_def.life_variance = attr("lifeVariance", 0);
    effect_def.size = static_cast<int>(attr("size", 3));
    effect_def.rise = attr("rise", 3);
    effect_def.rise_variance = attr("riseVariance", 0);
    effect_def.rise_acc = attr("riseAcc", 0);
    effect_def.range_x = static_cast<int>(attr("rangeX", 0));
    effect_def.range_y = static_cast<int>(attr("rangeY", 0));
    effect_def.z_offset = attr("zOffset", 0);
    effect_def.bitmap_sheet = sheet(effect.attribute("bitmapFile"), false);
    effect_def.bitmap_index = static_cast<int>(attr("bitmapIndex", 0));
  }
  for (auto alternate : xml.children("AltTexture")) {
    result.alternate.emplace(static_cast<int>(realm::number(alternate.attribute("id"))),
                             texture(alternate));
  }
  return result;
}

Catalog::Catalog(const std::filesystem::path& root)
    : documents_(std::make_unique<Documents>()),
      object_lookup_(65536, -1),
      ground_lookup_(65536, -1) {
  objects_.reserve(3000);
  grounds_.reserve(256);
  for (const auto& file : flash::data_files) {
    const auto path = root / file.path;
    auto* doc = xmlReadFile(path.string().c_str(), nullptr, XML_PARSE_NONET | XML_PARSE_NOBLANKS);
    if (!doc) {
      throw std::runtime_error("Cannot load definition XML: " + path.string());
    }
    documents_->docs.push_back(doc);
    const Xml xml(xmlDocGetRootElement(doc));
    if (file.kind == "groundFiles") {
      for (auto node : xml.children("Ground")) {
        GroundDef ground;
        ground.xml = node;
        ground.type = static_cast<std::uint16_t>(realm::number(node.attribute("type")));
        ground.id = node.attribute("id");
        ground.texture = texture(node);
        ground.edge = texture(node.child("Edge"));
        ground.corner = texture(node.child("Corner"));
        ground.inner_corner = texture(node.child("InnerCorner"));
        ground.top = texture(node.child("Top"));
        ground.no_walk = node.has("NoWalk");
        ground.damage = node.integer("Damage");
        ground.push = node.has("Push");
        ground.speed = node.number("Speed", 1);
        ground.blend_priority = node.integer("BlendPriority", -1);
        ground.composite_priority = node.integer("CompositePriority");
        ground.offset_x = node.number("XOffset");
        ground.offset_y = node.number("YOffset");
        ground.sink = node.has("Sink");
        ground.sinking = node.has("Sinking");
        ground.random_offset = node.has("RandomOffset");
        ground.same_type_edge = node.has("SameTypeEdgeMode");
        auto animation = node.child("Animate");
        ground.animate = animate(animation);
        ground.animate_x = realm::number(animation.attribute("dx"));
        ground.animate_y = realm::number(animation.attribute("dy"));
        animation = node.child("TopAnimate");
        ground.top_animate = animate(animation);
        ground.top_animate_x = realm::number(animation.attribute("dx"));
        ground.top_animate_y = realm::number(animation.attribute("dy"));

        ground_names_[ground.id] = ground.type;
        auto& index = ground_lookup_[ground.type];
        if (index < 0) {
          index = static_cast<std::int32_t>(grounds_.size());
          grounds_.push_back(std::move(ground));
        } else {
          grounds_[static_cast<std::size_t>(index)] = std::move(ground);
        }
      }
    }
    if (file.kind == "objectFiles") {
      for (auto node : xml.children("Object")) {
        ObjectDef object;
        object.xml = node;
        object.type = static_cast<std::uint16_t>(realm::number(node.attribute("type")));
        object.id = node.attribute("id");
        object.display_id = node.has("DisplayId") ? node.child("DisplayId").text() : object.id;
        object.class_name = node.child("Class").text();
        object.model = node.child("Model").text();
        object.container = object.class_name == "Container" || object.class_name == "OneWayContainer";
        object.interactive = object.container || interactive_class(object.class_name);
        object.texture = texture(node);
        object.top = texture(node.child("Top"));
        constexpr std::array<std::string_view, 6> connected_texture_names{
            "DotTexture",  "ShortLineTexture", "LTexture",
            "LineTexture", "TTexture",         "CrossTexture"};
        for (std::size_t i = 0; i < connected_texture_names.size(); ++i) {
          object.connected_textures[i] = texture(node.child(connected_texture_names[i]));
        }

        if (node.has("BaseIndent")) {
          object.wall_base_indent = 0.5 - node.number("BaseIndent");
        }
        if (node.has("TopIndent")) {
          object.wall_top_indent = 0.5 - node.number("TopIndent");
        }
        object.wall_height = node.number("Height", 1);
        object.wall_texture_repeat = !node.has("NoWallTextureRepeat");
        object.top_texture_repeat = !node.has("NoTopTextureRepeat");
        object.shadow_size = node.integer("ShadowSize", 100);
        object.player = node.has("Player");
        object.enemy = node.has("Enemy");
        object.draw_on_ground = node.has("DrawOnGround");
        object.draw_under = object.draw_on_ground || node.has("DrawUnder");
        object.occupy = node.has("OccupySquare");
        object.full_occupy = node.has("FullOccupy");
        object.enemy_occupy = node.has("EnemyOccupySquare");
        object.stationary = node.has("Static");
        object.no_minimap = node.has("NoMiniMap");
        object.protect_ground = node.has("ProtectFromGroundDamage");
        object.protect_sink = node.has("ProtectFromSink");
        object.flying = node.has("Flying");
        object.show_name = node.has("ShowName");
        object.nexus_portal = node.has("NexusPortal");
        object.dont_face_attacks = node.has("DontFaceAttacks");
        object.hexable = node.child("Group").text() == "Hexable";

        object.z = node.number("Z");
        object.rotation = node.number("Rotation");
        object.angle_correction = node.number("AngleCorrection") * std::numbers::pi / 4;
        object.color = static_cast<std::uint32_t>(node.number("Color", 0xffffff));
        object.blood_color = static_cast<std::uint32_t>(node.number("BloodColor", 0xff0000));
        object.blood_probability = node.number("BloodProb");
        object.shadow_color = static_cast<std::uint32_t>(node.number("ShadowColor"));
        object.min_size = node.integer("Size", node.integer("MinSize", 100));
        object.max_size = node.integer("Size", node.integer("MaxSize", 100));
        object.size_step = node.integer("SizeStep", 5);
        object.tex1 = static_cast<std::uint32_t>(node.number("Tex1"));
        object.tex2 = static_cast<std::uint32_t>(node.number("Tex2"));
        object.max_hp = node.integer("MaxHitPoints");
        object.defense = node.integer("Defense");
        object.slot_type = node.integer("SlotType", -1);
        object.tier = node.integer("Tier", -1);
        object.max_hp_cap = std::stoi(node.child("MaxHitPoints").attribute("max", "0"));
        object.max_mp = node.integer("MaxMagicPoints");
        object.max_mp_cap = std::stoi(node.child("MaxMagicPoints").attribute("max", "0"));
        constexpr std::array stat_names{"Attack",    "Defense", "Speed",
                                        "Dexterity", "HpRegen", "MpRegen"};
        for (std::size_t i = 0; i < stat_names.size(); ++i) {
          object.stat_caps[i] = std::stoi(node.child(stat_names[i]).attribute("max", "0"));
        }

        object.size = node.integer("Size", 100);
        object.num_projectiles = node.integer("NumProjectiles", 1);
        object.rate_of_fire = node.number("RateOfFire");
        object.arc_gap = node.number("ArcGap", 11.25) * std::numbers::pi / 180;
        object.mp_cost = node.integer("MpCost");
        object.cooldown_ms = static_cast<int>(node.number("Cooldown", .2) * 1000);
        object.usable = node.has("Usable");
        object.consumable = node.has("Consumable");
        object.inventory_use = node.has("InvUse");

        // Character.as: the Character constructor applies these defaults to Character and Player
        // instances only.
        if (object.class_name == "Character" || object.class_name == "Player") {
          object.hit_sound = node.has("HitSound") ? node.child("HitSound").text() : "monster/default_hit";
          object.death_sound =
              node.has("DeathSound") ? node.child("DeathSound").text() : "monster/default_death";
        }
        if (node.has("OldSound")) {
          object.old_sound = node.child("OldSound").text();
        }
        for (const auto sound : node.children("Sound")) {
          object.sounds.push_back(sound.text());
        }
        object.loot = node.has("Loot");
        const auto activations = node.children("Activate");
        object.ability_shoot = activations.size() == 1 && activations.front().text() == "Shoot";
        for (const auto activation : activations) {
          const auto kind = activation.text();
          if (kind == "Teleport") {
            object.ability_teleport = object.ability_target = true;
          } else if (kind == "BulletNova" || kind == "PoisonGrenade" || kind == "VampireBlast" ||
                     kind == "Trap" || kind == "StasisBlast") {
            object.ability_target = true;
          }
        }
        object.slot_types = integers(node.child("SlotTypes").text());
        for (auto child : node.children("Projectile")) {
          ProjectileDef projectile;
          projectile.xml = child;
          projectile.id = static_cast<int>(realm::number(child.attribute("id")));
          projectile.object_id = child.child("ObjectId").text();
          projectile.size = child.integer("Size", -1);
          projectile.min_damage = child.integer("Damage", child.integer("MinDamage"));
          projectile.max_damage = child.integer("Damage", child.integer("MaxDamage"));
          auto& motion = projectile.motion;
          motion.speed = child.integer("Speed");
          motion.lifetime_ms = child.integer("LifetimeMS");
          motion.magnitude = child.number("Magnitude", 3);
          motion.amplitude = child.number("Amplitude");
          motion.frequency = child.number("Frequency", 1);
          motion.accelerate = child.has("Accelerate");
          motion.decelerate = child.has("Decelerate");
          motion.wavy = child.has("Wavy");
          motion.parametric = child.has("Parametric");
          motion.boomerang = child.has("Boomerang");
          projectile.multi_hit = child.has("MultiHit");
          projectile.passes_cover = child.has("PassesCover");
          projectile.armor_piercing = child.has("ArmorPiercing");
          projectile.particle_trail = child.has("ParticleTrail");
          object.projectiles.push_back(std::move(projectile));
        }

        object_names_[object.id] = object.type;
        if (object.hexable) {
          hex_transforms_.push_back(object.type);
        }
        auto& index = object_lookup_[object.type];
        if (index < 0) {
          index = static_cast<std::int32_t>(objects_.size());
          objects_.push_back(std::move(object));
        } else {
          objects_[static_cast<std::size_t>(index)] = std::move(object);
        }
      }
    }
  }
  const auto particle_path = root / "src/kabam/rotmg/assets/EmbeddedAssets_particlesEmbed.dat";
  auto* particles_doc =
      xmlReadFile(particle_path.string().c_str(), nullptr, XML_PARSE_NONET | XML_PARSE_NOBLANKS);
  if (!particles_doc) {
    throw std::runtime_error("Cannot load particle definitions");
  }
  documents_->docs.push_back(particles_doc);
  for (const auto xml : Xml(xmlDocGetRootElement(particles_doc)).children("Particle")) {
    ParticleDef particle;
    particle.id = xml.attribute("id");
    particle.texture = texture(xml);
    particle.size = xml.integer("Size", 100);
    particle.z = xml.number("Z");
    particle.duration_ms = xml.number("Duration") * 1000;
    for (const auto child : xml.children("Animation")) {
      ParticleAnimationDef animation;
      animation.probability = realm::number(child.attribute("prob"), 1);
      for (const auto frame : child.children("Frame")) {
        animation.frames.push_back({texture(frame), realm::number(frame.attribute("time")) * 1000});
      }
      particle.animations.push_back(std::move(animation));
    }
    particles_.push_back(std::move(particle));
  }
  const auto path = root / "src/kabam/rotmg/assets/EmbeddedData_SkinsCXML.dat";
  auto* doc = xmlReadFile(path.string().c_str(), nullptr, XML_PARSE_NONET | XML_PARSE_NOBLANKS);
  if (!doc) {
    throw std::runtime_error("Cannot load skin definitions");
  }
  documents_->docs.push_back(doc);
  for (const auto xml : Xml(xmlDocGetRootElement(doc)).children("Object")) {
    skins_.push_back({static_cast<int>(realm::number(xml.attribute("type"))),
                      xml.integer("PlayerClassType"), xml.attribute("id"), texture(xml)});
  }
}

const ParticleDef* Catalog::particle(std::string_view id) const {
  for (const auto& particle : particles_) {
    if (particle.id == id) {
      return &particle;
    }
  }
  return nullptr;
}

const SkinDef* Catalog::skin(int id, int class_type) const {
  for (const auto& skin : skins_) {
    if (skin.id == id && skin.class_type == class_type) {
      return &skin;
    }
  }
  return nullptr;
}

const TextureDef& Catalog::player_texture(int class_type, int skin_id) const {
  if (const auto* entry = skin(skin_id, class_type)) {
    return entry->texture;
  }
  const auto* definition = object(static_cast<std::uint16_t>(class_type));
  if (!definition) {
    throw std::runtime_error("Unknown player class");
  }
  return definition->texture;
}

int Catalog::portrait_type(int class_type, int skin_id) const {
  return skin(skin_id, class_type) ? skin_id : class_type;
}

const ObjectDef* Catalog::object(std::uint16_t type) const {
  const auto index = object_lookup_[type];
  return index < 0 ? nullptr : &objects_[static_cast<std::size_t>(index)];
}

const GroundDef* Catalog::ground(std::uint16_t type) const {
  const auto index = ground_lookup_[type];
  return index < 0 ? nullptr : &grounds_[static_cast<std::size_t>(index)];
}

const ObjectDef* Catalog::object(std::string_view id) const {
  const auto it = object_names_.find(std::string(id));
  return it == object_names_.end() ? nullptr : object(it->second);
}

const GroundDef* Catalog::ground(std::string_view id) const {
  const auto it = ground_names_.find(std::string(id));
  return it == ground_names_.end() ? nullptr : ground(it->second);
}
}
