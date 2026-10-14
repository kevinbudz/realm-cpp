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
    "Portal",      "GuildHallPortal", "SellableObject", "ClosedVaultChest", "Merchant",   "GuildMerchant",
    "NameChanger", "ReskinVendor",    "GuildChronicle", "GuildRegister",    "GuildBoard", "CharacterChanger"};
bool interactive_class(std::string_view name) {
  return std::find(interactive_class_names.begin(), interactive_class_names.end(), name) !=
         interactive_class_names.end();
}
int sheet(std::string_view name, bool animated) {
  if (animated) {
    for (std::size_t i = 0; i < flash::animated.size(); ++i)
      if (flash::animated[i].name == name) return static_cast<int>(i);
  } else {
    for (std::size_t i = 0; i < flash::sheets.size(); ++i)
      if (flash::sheets[i].name == name) return static_cast<int>(i);
  }
  return -1;
}
std::vector<int> integers(std::string text) {
  std::replace(text.begin(), text.end(), ',', ' ');
  std::istringstream stream(text);
  std::vector<int> values;
  int value{};
  while (stream >> value) values.push_back(value);
  return values;
}
int animate(Xml xml) {
  const auto t = xml.text();
  return t == "Wave" ? 1 : t == "Flow" ? 2 : 0;
}
}
struct Catalog::Documents {
  std::vector<xmlDocPtr> docs;
  ~Documents() {
    for (auto* doc : docs) xmlFreeDoc(doc);
  }
};
Catalog::~Catalog() = default;
TextureDef Catalog::texture(Xml xml) {
  TextureDef t;
  if (!xml) return t;
  const auto kind = xml.name();
  // ConnectedWall's DotTexture/ShortLineTexture/LTexture/LineTexture/TTexture/CrossTexture
  // elements use a bare <File>/<Index> pair, unlike the normal <Texture> wrapper.
  if (kind != "Texture" && kind != "AnimatedTexture" && kind != "Mask" && xml.has("File")) {
    const auto file = xml.child("File").text();
    t.sheet = sheet(file, false);
    t.index = xml.integer("Index");
    if (t.sheet < 0 && std::find(unresolved_.begin(), unresolved_.end(), file) == unresolved_.end())
      unresolved_.push_back(file);
    return t;
  }
  if (kind == "Texture" || kind == "AnimatedTexture" || kind == "Mask") {
    const auto file = xml.child("File").text();
    t.index = xml.integer("Index");
    if (kind == "AnimatedTexture")
      t.animated = sheet(file, true);
    else
      t.sheet = sheet(file, false);
    if (t.sheet < 0 && t.animated < 0 && std::find(unresolved_.begin(), unresolved_.end(), file) == unresolved_.end())
      unresolved_.push_back(file);
    return t;
  }
  if (kind == "RandomTexture") {
    for (auto child : xml.children())
      if (child.name() == "Texture" || child.name() == "AnimatedTexture" || child.name() == "RandomTexture")
        t.random.push_back(texture(child));
    return t;
  }
  if (xml.has("Texture"))
    t = texture(xml.child("Texture"));
  else if (xml.has("AnimatedTexture"))
    t = texture(xml.child("AnimatedTexture"));
  else if (xml.has("RandomTexture"))
    t = texture(xml.child("RandomTexture"));
  if (xml.has("Mask")) {
    const auto mask = texture(xml.child("Mask"));
    t.mask_sheet = mask.sheet;
    t.mask_index = mask.index;
  }
  if (const auto effect = xml.child("Effect")) {
    auto& e = t.effect;
    e.kind = effect.text();
    e.particle = effect.attribute("particle");
    const auto attr = [&](std::string_view key, double fallback) {
      const double v = realm::number(effect.attribute(key));
      return v == 0 ? fallback : v;
    };
    e.color = static_cast<std::uint32_t>(attr("color", 0));
    e.cooldown = attr("cooldown", 0);
    e.rate = attr("rate", 5);
    e.speed = attr("speed", 0);
    e.speed_variance = attr("speedVariance", .5);
    e.spread = attr("spread", 0);
    e.life = attr("life", 1);
    e.life_variance = attr("lifeVariance", 0);
    e.size = static_cast<int>(attr("size", 3));
    e.rise = attr("rise", 3);
    e.rise_variance = attr("riseVariance", 0);
    e.rise_acc = attr("riseAcc", 0);
    e.range_x = static_cast<int>(attr("rangeX", 0));
    e.range_y = static_cast<int>(attr("rangeY", 0));
    e.z_offset = attr("zOffset", 0);
    e.bitmap_sheet = sheet(effect.attribute("bitmapFile"), false);
    e.bitmap_index = static_cast<int>(attr("bitmapIndex", 0));
  }
  for (auto alternate : xml.children("AltTexture"))
    t.alternate.emplace(static_cast<int>(realm::number(alternate.attribute("id"))), texture(alternate));
  return t;
}
Catalog::Catalog(const std::filesystem::path& root)
    : documents_(std::make_unique<Documents>()), object_lookup_(65536, -1), ground_lookup_(65536, -1) {
  objects_.reserve(3000);
  grounds_.reserve(256);
  for (const auto& file : flash::data_files) {
    const auto path = root / file.path;
    auto* doc = xmlReadFile(path.string().c_str(), nullptr, XML_PARSE_NONET | XML_PARSE_NOBLANKS);
    if (!doc) throw std::runtime_error("Cannot load definition XML: " + path.string());
    documents_->docs.push_back(doc);
    const Xml xml(xmlDocGetRootElement(doc));
    if (file.kind == "groundFiles")
      for (auto g : xml.children("Ground")) {
        GroundDef d;
        d.xml = g;
        d.type = static_cast<std::uint16_t>(realm::number(g.attribute("type")));
        d.id = g.attribute("id");
        d.texture = texture(g);
        d.edge = texture(g.child("Edge"));
        d.corner = texture(g.child("Corner"));
        d.inner_corner = texture(g.child("InnerCorner"));
        d.top = texture(g.child("Top"));
        d.no_walk = g.has("NoWalk");
        d.damage = g.integer("Damage");
        d.push = g.has("Push");
        d.speed = g.number("Speed", 1);
        d.blend_priority = g.integer("BlendPriority", -1);
        d.composite_priority = g.integer("CompositePriority");
        d.offset_x = g.number("XOffset");
        d.offset_y = g.number("YOffset");
        d.sink = g.has("Sink");
        d.sinking = g.has("Sinking");
        d.random_offset = g.has("RandomOffset");
        d.same_type_edge = g.has("SameTypeEdgeMode");
        auto a = g.child("Animate");
        d.animate = animate(a);
        d.animate_x = realm::number(a.attribute("dx"));
        d.animate_y = realm::number(a.attribute("dy"));
        a = g.child("TopAnimate");
        d.top_animate = animate(a);
        d.top_animate_x = realm::number(a.attribute("dx"));
        d.top_animate_y = realm::number(a.attribute("dy"));
        ground_names_[d.id] = d.type;
        auto& index = ground_lookup_[d.type];
        if (index < 0) {
          index = static_cast<std::int32_t>(grounds_.size());
          grounds_.push_back(std::move(d));
        } else
          grounds_[static_cast<std::size_t>(index)] = std::move(d);
      }
    if (file.kind == "objectFiles")
      for (auto o : xml.children("Object")) {
        ObjectDef d;
        d.xml = o;
        d.type = static_cast<std::uint16_t>(realm::number(o.attribute("type")));
        d.id = o.attribute("id");
        d.display_id = o.has("DisplayId") ? o.child("DisplayId").text() : d.id;
        d.class_name = o.child("Class").text();
        d.model = o.child("Model").text();
        d.container = d.class_name == "Container" || d.class_name == "OneWayContainer";
        d.interactive = d.container || interactive_class(d.class_name);
        d.texture = texture(o);
        d.top = texture(o.child("Top"));
        constexpr std::array<std::string_view, 6> connected_texture_names{
            "DotTexture", "ShortLineTexture", "LTexture", "LineTexture", "TTexture", "CrossTexture"};
        for (std::size_t i = 0; i < connected_texture_names.size(); ++i)
          d.connected_textures[i] = texture(o.child(connected_texture_names[i]));
        if (o.has("BaseIndent")) d.wall_base_indent = 0.5 - o.number("BaseIndent");
        if (o.has("TopIndent")) d.wall_top_indent = 0.5 - o.number("TopIndent");
        d.wall_height = o.number("Height", 1);
        d.wall_texture_repeat = !o.has("NoWallTextureRepeat");
        d.top_texture_repeat = !o.has("NoTopTextureRepeat");
        d.shadow_size = o.integer("ShadowSize", 100);
        d.player = o.has("Player");
        d.enemy = o.has("Enemy");
        d.draw_on_ground = o.has("DrawOnGround");
        d.draw_under = d.draw_on_ground || o.has("DrawUnder");
        d.occupy = o.has("OccupySquare");
        d.full_occupy = o.has("FullOccupy");
        d.enemy_occupy = o.has("EnemyOccupySquare");
        d.stationary = o.has("Static");
        d.no_minimap = o.has("NoMiniMap");
        d.protect_ground = o.has("ProtectFromGroundDamage");
        d.protect_sink = o.has("ProtectFromSink");
        d.flying = o.has("Flying");
        d.show_name = o.has("ShowName");
        d.dont_face_attacks = o.has("DontFaceAttacks");
        d.hexable = o.child("Group").text() == "Hexable";
        d.z = o.number("Z");
        d.rotation = o.number("Rotation");
        d.angle_correction = o.number("AngleCorrection") * std::numbers::pi / 4;
        d.color = static_cast<std::uint32_t>(o.number("Color", 0xffffff));
        d.blood_color = static_cast<std::uint32_t>(o.number("BloodColor", 0xff0000));
        d.blood_probability = o.number("BloodProb");
        d.shadow_color = static_cast<std::uint32_t>(o.number("ShadowColor"));
        d.min_size = o.integer("Size", o.integer("MinSize", 100));
        d.max_size = o.integer("Size", o.integer("MaxSize", 100));
        d.size_step = o.integer("SizeStep", 5);
        d.tex1 = static_cast<std::uint32_t>(o.number("Tex1"));
        d.tex2 = static_cast<std::uint32_t>(o.number("Tex2"));
        d.max_hp = o.integer("MaxHitPoints");
        d.defense = o.integer("Defense");
        d.slot_type = o.integer("SlotType", -1);
        d.tier = o.integer("Tier", -1);
        d.max_hp_cap = std::stoi(o.child("MaxHitPoints").attribute("max", "0"));
        d.max_mp = o.integer("MaxMagicPoints");
        d.max_mp_cap = std::stoi(o.child("MaxMagicPoints").attribute("max", "0"));
        constexpr std::array stat_names{"Attack", "Defense", "Speed", "Dexterity", "HpRegen", "MpRegen"};
        for (std::size_t i = 0; i < stat_names.size(); ++i)
          d.stat_caps[i] = std::stoi(o.child(stat_names[i]).attribute("max", "0"));
        d.size = o.integer("Size", 100);
        d.num_projectiles = o.integer("NumProjectiles", 1);
        d.rate_of_fire = o.number("RateOfFire");
        d.arc_gap = o.number("ArcGap", 11.25) * std::numbers::pi / 180;
        d.mp_cost = o.integer("MpCost");
        d.cooldown_ms = static_cast<int>(o.number("Cooldown", .2) * 1000);
        d.usable = o.has("Usable");
        d.consumable = o.has("Consumable");
        d.inventory_use = o.has("InvUse");
        // Character.as: the Character constructor applies these defaults to Character and Player instances only.
        if (d.class_name == "Character" || d.class_name == "Player") {
          d.hit_sound = o.has("HitSound") ? o.child("HitSound").text() : "monster/default_hit";
          d.death_sound = o.has("DeathSound") ? o.child("DeathSound").text() : "monster/default_death";
        }
        if (o.has("OldSound")) d.old_sound = o.child("OldSound").text();
        for (const auto sound : o.children("Sound")) d.sounds.push_back(sound.text());
        d.loot = o.has("Loot");
        const auto activations = o.children("Activate");
        d.ability_shoot = activations.size() == 1 && activations.front().text() == "Shoot";
        for (const auto a : activations) {
          const auto kind = a.text();
          if (kind == "Teleport")
            d.ability_teleport = d.ability_target = true;
          else if (kind == "BulletNova" || kind == "PoisonGrenade" || kind == "VampireBlast" || kind == "Trap" ||
                   kind == "StasisBlast")
            d.ability_target = true;
        }
        d.slot_types = integers(o.child("SlotTypes").text());
        for (auto p : o.children("Projectile")) {
          ProjectileDef pd;
          pd.xml = p;
          pd.id = static_cast<int>(realm::number(p.attribute("id")));
          pd.object_id = p.child("ObjectId").text();
          pd.size = p.integer("Size", -1);
          pd.min_damage = p.integer("Damage", p.integer("MinDamage"));
          pd.max_damage = p.integer("Damage", p.integer("MaxDamage"));
          auto& m = pd.motion;
          m.speed = p.integer("Speed");
          m.lifetime_ms = p.integer("LifetimeMS");
          m.magnitude = p.number("Magnitude", 3);
          m.amplitude = p.number("Amplitude");
          m.frequency = p.number("Frequency", 1);
          m.accelerate = p.has("Accelerate");
          m.decelerate = p.has("Decelerate");
          m.wavy = p.has("Wavy");
          m.parametric = p.has("Parametric");
          m.boomerang = p.has("Boomerang");
          pd.multi_hit = p.has("MultiHit");
          pd.passes_cover = p.has("PassesCover");
          pd.armor_piercing = p.has("ArmorPiercing");
          pd.particle_trail = p.has("ParticleTrail");
          d.projectiles.push_back(std::move(pd));
        }
        object_names_[d.id] = d.type;
        if (d.hexable) hex_transforms_.push_back(d.type);
        auto& index = object_lookup_[d.type];
        if (index < 0) {
          index = static_cast<std::int32_t>(objects_.size());
          objects_.push_back(std::move(d));
        } else
          objects_[static_cast<std::size_t>(index)] = std::move(d);
      }
  }
  const auto particle_path = root / "src/kabam/rotmg/assets/EmbeddedAssets_particlesEmbed.dat";
  auto* particles_doc = xmlReadFile(particle_path.string().c_str(), nullptr, XML_PARSE_NONET | XML_PARSE_NOBLANKS);
  if (!particles_doc) throw std::runtime_error("Cannot load particle definitions");
  documents_->docs.push_back(particles_doc);
  for (const auto xml : Xml(xmlDocGetRootElement(particles_doc)).children("Particle")) {
    ParticleDef p;
    p.id = xml.attribute("id");
    p.texture = texture(xml);
    p.size = xml.integer("Size", 100);
    p.z = xml.number("Z");
    p.duration_ms = xml.number("Duration") * 1000;
    for (const auto a : xml.children("Animation")) {
      ParticleAnimationDef animation;
      animation.probability = realm::number(a.attribute("prob"), 1);
      for (const auto f : a.children("Frame"))
        animation.frames.push_back({texture(f), realm::number(f.attribute("time")) * 1000});
      p.animations.push_back(std::move(animation));
    }
    particles_.push_back(std::move(p));
  }
  const auto path = root / "src/kabam/rotmg/assets/EmbeddedData_SkinsCXML.dat";
  auto* doc = xmlReadFile(path.string().c_str(), nullptr, XML_PARSE_NONET | XML_PARSE_NOBLANKS);
  if (!doc) throw std::runtime_error("Cannot load skin definitions");
  documents_->docs.push_back(doc);
  for (const auto xml : Xml(xmlDocGetRootElement(doc)).children("Object"))
    skins_.push_back({static_cast<int>(realm::number(xml.attribute("type"))), xml.integer("PlayerClassType"),
                      xml.attribute("id"), texture(xml)});
}
const ParticleDef* Catalog::particle(std::string_view id) const {
  for (const auto& p : particles_)
    if (p.id == id) return &p;
  return nullptr;
}
const SkinDef* Catalog::skin(int id, int class_type) const {
  for (const auto& s : skins_)
    if (s.id == id && s.class_type == class_type) return &s;
  return nullptr;
}
const TextureDef& Catalog::player_texture(int class_type, int skin_id) const {
  if (const auto* s = skin(skin_id, class_type)) return s->texture;
  const auto* d = object(static_cast<std::uint16_t>(class_type));
  if (!d) throw std::runtime_error("Unknown player class");
  return d->texture;
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
