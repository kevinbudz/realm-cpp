#include "realm/objects/StatusArt.hpp"
#include "realm/assets/AnimationLibrary.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

// Rules from ConditionEffect, GameObject.damage, CharacterStatusText, Player and
// GameServerConnection that need no GL context or fonts.
namespace {
int checks{};

void check(bool ok, const std::string& why) {
  ++checks;
  if (!ok) {
    throw std::runtime_error(why);
  }
}
}

using namespace realm;

namespace {
void near(double actual, double expected, double tolerance, const std::string& why) {
  check(std::abs(actual - expected) <= tolerance,
        why + ": got " + std::to_string(actual) + " expected " + std::to_string(expected));
}

using StatId = flash::StatId;

constexpr std::uint8_t stat(StatId id) {
  return static_cast<std::uint8_t>(id);
}

void conditions() {
  // Bit = 1 << (id - 1), so the existing magic numbers elsewhere in the client agree with the
  // table.
  check(condition::bit(condition::quiet) == 1 && condition::bit(condition::weak) == 2 &&
            condition::bit(condition::slowed) == 4,
        "low bits");
  check(condition::bit(condition::blind) == 1U << 6 && condition::bit(condition::drunk) == 1U << 8,
        "blind/drunk match the post effects");
  check(condition::bit(condition::invisible) == 1U << 11 &&
            condition::bit(condition::healing) == 1U << 15,
        "invisible/healing");
  check(condition::bit(condition::invincible) == 1U << 20 &&
            condition::bit(condition::invulnerable) == 1U << 21 &&
            condition::bit(condition::armor_broken) == 1U << 23,
        "invincible/invulnerable/armor broken");
  check(condition::bit(condition::hexed) == 1U << 24 && condition::bit(condition::nothing) == 0,
        "hexed and nothing");
  check(condition_effect(condition::armor_broken).name == "Armor Broken" &&
            condition_effect(condition::stun_immune).name == "Stun Immune" &&
            condition_effect(999).name == "Nothing",
        "names");
  std::vector<std::uint8_t> frames;
  condition_icon_frames(0, 0, frames);
  check(frames.empty(), "no condition, no icons");
  // Conditions without icons add nothing.
  condition_icon_frames(condition::bit(condition::invisible) | condition::bit(condition::stasis) |
                            condition::bit(condition::invincible) |
                            condition::bit(condition::stun_immune) |
                            condition::bit(condition::stasis_immune),
                        0, frames);
  check(frames.empty(), "icon-less conditions");
  // Ascending bit order, one frame each; Weak cycles four frames and Paralyzed two, every 500 ms.
  const auto mask = condition::bit(condition::paralyzed) | condition::bit(condition::weak) |
                    condition::bit(condition::slowed) | condition::bit(condition::hexed);
  condition_icon_frames(mask, 0, frames);
  check((frames == std::vector<std::uint8_t>{34, 1, 53, 42}), "frames at 0 ms");
  condition_icon_frames(mask, 500, frames);
  check((frames == std::vector<std::uint8_t>{35, 1, 54, 42}), "frames at 500 ms");
  condition_icon_frames(mask, 1999, frames);
  check((frames == std::vector<std::uint8_t>{37, 1, 54, 42}), "frames at 1999 ms");
  condition_icon_frames(mask, 2000, frames);
  check((frames == std::vector<std::uint8_t>{34, 1, 53, 42}), "frames wrap");
  for (unsigned id = 1; id < condition::count; ++id) {
    const auto& effect = condition_effect(id);
    check(effect.icon_count <= 4, "at most four frames");
    const auto sheet =
        std::find_if(flash::sheets.begin(), flash::sheets.end(),
                     [](const auto& value) { return value.name == "lofiInterface2"; });
    check(sheet != flash::sheets.end(), "lofiInterface2 sheet exists");
    for (int i = 0; i < effect.icon_count; ++i) {
      check(effect.icons[static_cast<std::size_t>(i)] < sheet->count,
            "frame inside lofiInterface2");
    }
  }
}

void bars() {
  check(breath_bar_back_color(100, 0) == 0x545454U && breath_bar_back_color(21, 12345) == 0x545454U,
        "plain back above the threshold");
  check(breath_bar_back_color(20, 12345) == 0x545454U, "threshold itself is still plain (b = 0)");
  const auto peak = static_cast<std::int32_t>(300 * std::numbers::pi / 2);
  near(static_cast<double>(breath_bar_back_color(0, peak) >> 16U), 255, 2,
       "empty breath peaks at red");
  const auto half = breath_bar_back_color(10, peak);
  near(static_cast<double>(half >> 16U), (0x54 + (255 - 0x54) * .5), 2, "half breath: red halfway");
  near(static_cast<double>(half & 255U), 0x54 * .5, 2, "half breath: blue halfway");
  check(breath_bar_back_color(0, 0) == 0x545454U, "|sin(0)| = 0");
  near(low_health_pulse(0), 0, 1e-9, "pulse starts at 0");
  near(low_health_pulse(static_cast<std::int32_t>(200 * std::numbers::pi / 2)), .9, 1e-9,
       "pulse peaks just under 1 (int() truncates 9.99)");
  for (int t = 0; t < 5000; t += 13) {
    const auto rv = low_health_pulse(t);
    check(rv >= 0 && rv <= 1 && std::abs(rv * 10 - std::round(rv * 10)) < 1e-9,
          "pulse is quantized to tenths");
  }
}

// A world with a player, an enemy, and a bystander, built without a map.
struct Fixture {
  Catalog catalog{flash::source_root};
  World world{catalog};

  Fixture() {
    world.initialize(8, 8, "status");
    world.add(1, catalog.object("Wizard")->type, {2.5, 2.5});
    world.set_player_id(1);
    world.entity(1)->combat.hp = world.entity(1)->combat.max_hp = 100;
    world.add(2, catalog.object("Wizard")->type, {4.5, 2.5});
    world.entity(2)->combat.hp = world.entity(2)->combat.max_hp = 100;
  }

  void tick(std::int32_t id, std::vector<net::Stat> stats) {
    net::Tick t;
    net::Status s;
    s.id = id;
    s.position = {static_cast<float>(world.entity(id)->motion.position.x),
                  static_cast<float>(world.entity(id)->motion.position.y)};
    s.stats = std::move(stats);
    t.statuses.push_back(s);
    world.apply(t);
  }
};

void events() {
  Fixture f;
  // Plain damage: red -N for one second.
  f.world.report_damage(2, 37, 1000);
  auto events = f.world.take_status_events();
  check(events.size() == 1 && events[0].object_id == 2 && events[0].text == "-37" &&
            events[0].color == 0xff0000U && events[0].lifetime == 1000 &&
            events[0].offset_time == 0,
        "damage text");
  check(f.world.take_status_events().empty(), "events are handed out once");
  // Piercing shots and armor-broken targets show purple.
  f.world.report_damage(2, 5, 1000, {}, true);
  events = f.world.take_status_events();
  check(events.size() == 1 && events[0].color == 0x9000ffU, "armor piercing is purple");
  f.world.entity(2)->combat.condition = condition::bit(condition::armor_broken);
  f.world.report_damage(2, 5, 1000);
  events = f.world.take_status_events();
  check(events.size() == 1 && events[0].color == 0x9000ffU, "armor broken target is purple");
  f.world.entity(2)->combat.condition = 0;
  // Zero damage shows no number. Condition names come first, 500 ms apart, red for three seconds;
  // conditions without text (Healing, Berserk, Invulnerable...) and NOTHING add none.
  const std::array<std::uint8_t, 6> effects{condition::slowed,  condition::nothing,
                                            condition::healing, condition::dazed,
                                            condition::berserk, condition::stasis};
  f.world.report_damage(2, 0, 1000, effects);
  events = f.world.take_status_events();
  check(events.size() == 3, "three displayed conditions");
  check(events[0].text == "Slowed" && events[0].offset_time == 0 && events[1].text == "Dazed" &&
            events[1].offset_time == 500 && events[2].text == "Stasis" &&
            events[2].offset_time == 1000,
        "names staggered by 500 ms");
  for (const auto& e : events) {
    check(e.color == 0xff0000U && e.lifetime == 3000, "condition text is red for 3 s");
  }
  f.world.report_damage(2, 12, 1000, std::array<std::uint8_t, 1>{condition::weak});
  events = f.world.take_status_events();
  check(events.size() == 2 && events[0].text == "Weak" && events[1].text == "-12",
        "conditions precede the damage number");
  // Stunned: shown, unless the target is stun immune, in which case "Immune" appears and no offset
  // is consumed.
  f.world.report_damage(2, 0, 1000,
                        std::array<std::uint8_t, 2>{condition::stunned, condition::slowed});
  events = f.world.take_status_events();
  check(events.size() == 2 && events[0].text == "Stunned" && events[1].text == "Slowed" &&
            events[1].offset_time == 500,
        "stunned shows");
  f.world.entity(2)->combat.condition = condition::bit(condition::stun_immune);
  f.world.report_damage(2, 0, 1000,
                        std::array<std::uint8_t, 2>{condition::stunned, condition::slowed});
  events = f.world.take_status_events();
  check(events.size() == 2 && events[0].text == "Immune" && events[0].lifetime == 3000 &&
            events[1].text == "Slowed" && events[1].offset_time == 0,
        "stun immune");
  // Objects that are gone get no text; the queue is bounded when nothing drains it.
  f.world.report_damage(77, 10, 1000);
  check(f.world.take_status_events().empty(), "unknown object");
  for (int i = 0; i < 200; ++i) {
    f.world.notify(2, "x" + std::to_string(i), 0xffffffU, 100);
  }
  events = f.world.take_status_events();
  check(events.size() == 64 && events.back().text == "x199" && events.front().text == "x136",
        "bounded, newest kept");
  f.world.notify(2, "kept", 1, 1);
  f.world.initialize(8, 8, "again");
  check(f.world.take_status_events().empty(), "a new map clears pending text");
}

void progress() {
  Fixture f;
  // A player first seen at some level never pops text; increases afterward do.
  f.tick(2, {{stat(StatId::LEVEL_STAT), 5},
             {stat(StatId::EXP_STAT), 10},
             {stat(StatId::CHAR_FAME_STAT), 3}});
  check(f.world.take_status_events().empty(), "first sighting is silent");
  f.tick(2, {{stat(StatId::EXP_STAT), 35}});
  auto events = f.world.take_status_events();
  check(events.size() == 1 && events[0].text == "+25 EXP" && events[0].color == 0x00ff00U &&
            events[0].lifetime == 1000 && events[0].object_id == 2,
        "exp gain");
  f.tick(2, {{stat(StatId::EXP_STAT), 20}});
  check(f.world.take_status_events().empty(), "exp loss is silent");
  f.tick(2, {{stat(StatId::LEVEL_STAT), 6}, {stat(StatId::EXP_STAT), 40}});
  events = f.world.take_status_events();
  check(events.size() == 1 && events[0].text == "Level Up!" && events[0].color == 0x00ff00U &&
            events[0].lifetime == 2000,
        "level up wins over exp");
  // Fame appears only at level 20, where exp text does not.
  f.tick(2, {{stat(StatId::LEVEL_STAT), 20}, {stat(StatId::EXP_STAT), 40}});
  (void)f.world.take_status_events();
  f.tick(2, {{stat(StatId::EXP_STAT), 90}, {stat(StatId::CHAR_FAME_STAT), 13}});
  events = f.world.take_status_events();
  check(events.size() == 1 && events[0].text == "+10 Fame" && events[0].color == 0xe25f00U &&
            events[0].lifetime == 1000,
        "fame at level 20");
  f.tick(2, {{stat(StatId::CHAR_FAME_STAT), 30}});
  check(f.world.take_status_events().empty(), "fame alone (no exp) is silent");
  // The local player is handled the same way, and non-players never pop text.
  f.tick(1, {{stat(StatId::LEVEL_STAT), 1}, {stat(StatId::EXP_STAT), 0}});
  (void)f.world.take_status_events();
  f.tick(1, {{stat(StatId::LEVEL_STAT), 2}});
  events = f.world.take_status_events();
  check(events.size() == 1 && events[0].object_id == 1 && events[0].text == "Level Up!",
        "own level up");
  f.world.entity(2)->player = false;
  f.tick(2, {{stat(StatId::EXP_STAT), 500}});
  check(f.world.take_status_events().empty(), "only players");
}

void texts(FontBook& fonts) {
  Fixture f;
  StatusArt art(fonts, nullptr);
  StatusTexts texts;
  texts.add(f.world, {2, "-9", 0xff0000U, 1000, 500});
  texts.add(f.world, {99, "gone", 0, 1000, 0});
  check(texts.texts().size() == 1, "unknown objects are not queued");
  check(!StatusTexts::visible(texts.texts()[0], 0), "hidden before the clock starts");
  texts.update(f.world, art, 10000);
  const auto& text = texts.texts()[0];
  check(text.started && text.start == 10500, "timer starts on first update, plus the offset");
  check(!text.label.image, "no label is rasterized before the text is visible");
  texts.update(f.world, art, 10500);
  check(texts.texts()[0].label.image && texts.texts()[0].label.width > 0,
        "label rasterized once visible");
  check(!StatusTexts::visible(text, 10499) && StatusTexts::visible(text, 10500),
        "hidden until the offset has passed");
  // Fixed label metrics stand in for a rasterized label: 40x30 field, no padding.
  auto sized = text;
  sized.label.width = 40;
  sized.label.height = 30;
  sized.label.offset = {};
  sized.texture_height = 8;
  // drift = dt / lifetime * 40 above (0, -texture.height * size / 100 * 5 - 20), centered on the
  // label.
  auto at = StatusTexts::position(sized, {100, 200}, 100, 1, 10500);
  near(at.x, 80, 1e-9, "centered");
  near(at.y, 200 - 40 - 20 - 15, 1e-9, "above a 40 px sprite");
  at = StatusTexts::position(sized, {100, 200}, 100, 1, 11000);
  near(at.y, 200 - 40 - 20 - 15 - 20, 1e-9, "halfway: 20 px of drift");
  at = StatusTexts::position(sized, {100, 200}, 200, 2, 10500);
  near(at.x, 100 - 40, 1e-9, "zoom scales the label");
  near(at.y, 200 + (-80 - 20 - 15) * 2, 1e-9, "size 200 doubles the sprite offset");
  texts.update(f.world, art, 11500);
  check(texts.texts().size() == 1, "still alive at the end of its lifetime");
  texts.update(f.world, art, 11501);
  check(texts.texts().empty(), "expires after its lifetime");
  texts.add(f.world, {2, "a", 0, 1000, 0});
  texts.update(f.world, art, 0);
  f.world.remove(2);
  texts.update(f.world, art, 10);
  check(texts.texts().empty(), "dropped with its object");
  for (int i = 0; i < 100; ++i) {
    texts.add(f.world, {1, "t" + std::to_string(i), 0, 1000, 0});
  }
  check(texts.texts().size() == StatusTexts::capacity && texts.texts().back().text == "t99",
        "bounded, newest kept");
}

// The sprite width StatusArt reads for an object: its animated frame, else its sheet.
int sprite_width(const ObjectDef& definition) {
  const auto& texture = definition.texture;
  check(texture.animated >= 0 || texture.sheet >= 0, "the Wizard has a sprite");
  if (texture.animated >= 0) {
    return flash::animated[static_cast<std::size_t>(texture.animated)].width;
  }
  return flash::sheets[static_cast<std::size_t>(texture.sheet)].width;
}

void scatter() {
  Fixture f;
  // Only the damage number is randomized; condition names, Immune and level/exp text are plain.
  f.world.report_damage(2, 37, 1000, std::array<std::uint8_t, 1>{condition::weak});
  auto events = f.world.take_status_events();
  check(events.size() == 2 && events[0].text == "Weak" && !events[0].randomized &&
            events[1].text == "-37" && events[1].randomized && events[1].time_ms == 1000,
        "only the damage number is randomized");
  f.world.entity(2)->combat.condition = condition::bit(condition::stun_immune);
  f.world.report_damage(2, 0, 1000, std::array<std::uint8_t, 1>{condition::stunned});
  events = f.world.take_status_events();
  check(events.size() == 1 && events[0].text == "Immune" && !events[0].randomized,
        "Immune is plain");
  f.world.entity(2)->combat.condition = 0;
  f.tick(2, {{stat(StatId::LEVEL_STAT), 5}, {stat(StatId::EXP_STAT), 10}});
  (void)f.world.take_status_events();
  f.tick(2, {{stat(StatId::EXP_STAT), 35}});
  events = f.world.take_status_events();
  check(events.size() == 1 && events[0].text == "+25 EXP" && !events[0].randomized,
        "exp text is plain");

  // Scatter is within half the sprite's width, drift within 20 px, varies on both sides, and is
  // the same for the same events in the same order.
  const auto* wizard = f.catalog.object("Wizard");
  check(wizard != nullptr, "Wizard exists");
  const double half = sprite_width(*wizard) / 2.0;
  check(half > 0, "Wizard has a width");
  StatusTexts texts;
  StatusTexts again;
  for (int i = 0; i < 32; ++i) {
    const StatusEvent event{2, "-" + std::to_string(i), 0xff0000U, 1000, 0, true, 1000};
    texts.add(f.world, event);
    again.add(f.world, event);
  }
  check(texts.texts().size() == 32 && again.texts().size() == 32, "randomized texts are queued");
  bool positive = false, negative = false, varied = false;
  for (std::size_t i = 0; i < texts.texts().size(); ++i) {
    const auto& text = texts.texts()[i];
    check(text.randomized, "damage text is randomized");
    check(
        std::abs(text.offset_x) <= half && std::abs(text.drift_x) <= StatusTexts::randomized_drift,
        "offset within half the width, drift within 20 px");
    positive |= text.offset_x > 0;
    negative |= text.offset_x < 0;
    varied |= text.offset_x != texts.texts()[0].offset_x;
    check(text.offset_x == again.texts()[i].offset_x && text.drift_x == again.texts()[i].drift_x,
          "same inputs give the same scatter");
  }
  check(positive && negative && varied, "scatter varies on both sides");
  StatusTexts later;
  later.add(f.world, {2, "-0", 0xff0000U, 1000, 0, true, 1001});
  check(later.texts()[0].offset_x != texts.texts()[0].offset_x ||
            later.texts()[0].drift_x != texts.texts()[0].drift_x,
        "the report time is part of the key");

  // x = offset + sign(offset) * dt / lifetime * drift_x: drift moves a text away from the center on
  // its own side, and y keeps the plain upward drift. Label width 0 keeps the centering out.
  StatusText sized;
  sized.started = true;
  sized.start = 10000;
  sized.lifetime = 1000;
  sized.label.width = 0;
  sized.label.height = 30;
  sized.randomized = true;
  sized.offset_x = 10;
  sized.drift_x = 20;
  near(StatusTexts::position(sized, {100, 200}, 100, 1, 10000).x, 110, 1e-9, "offset at the start");
  near(StatusTexts::position(sized, {100, 200}, 100, 1, 10500).x, 120, 1e-9,
       "positive offset and drift: right");
  sized.offset_x = -10;
  near(StatusTexts::position(sized, {100, 200}, 100, 1, 10500).x, 80, 1e-9,
       "negative offset and drift: left");
  sized.drift_x = -20;
  near(StatusTexts::position(sized, {100, 200}, 100, 1, 10500).x, 100, 1e-9,
       "negative offset, negative drift: back to the center");
  sized.offset_x = 10;
  near(StatusTexts::position(sized, {100, 200}, 100, 1, 10500).x, 100, 1e-9,
       "positive offset, negative drift: back to the center");
  sized.offset_x = -10;
  sized.drift_x = 20;
  near(StatusTexts::position(sized, {100, 200}, 100, 2, 10500).x, 60, 1e-9,
       "zoom scales the sideways position");
  near(StatusTexts::position(sized, {100, 200}, 100, 1, 10500).y, 200 - 80 - 15, 1e-9,
       "vertical drift is unchanged");
  sized.randomized = false;
  near(StatusTexts::position(sized, {100, 200}, 100, 1, 10500).x, 100, 1e-9,
       "plain text ignores the scatter");

  // Alpha: 1 until 75% of the lifetime, 0.75 at the end; plain text stays opaque.
  sized.randomized = true;
  near(StatusTexts::opacity(sized, 10000), 1, 1e-9, "opaque at the start");
  near(StatusTexts::opacity(sized, 10750), 1, 1e-9, "opaque at 75%");
  near(StatusTexts::opacity(sized, 10800), 15.0 / 16, 1e-9, "80%: 15 / 16");
  near(StatusTexts::opacity(sized, 10900), 15.0 / 18, 1e-9, "90%: 15 / 18");
  near(StatusTexts::opacity(sized, 11000), 0.75, 1e-9, "0.75 at the end");
  sized.randomized = false;
  near(StatusTexts::opacity(sized, 11000), 1, 1e-9, "plain text stays opaque");
}

void art_cache(FontBook& fonts, const std::filesystem::path& root) {
  AnimationLibrary animations(root);
  Appearances appearances(root, animations);
  StatusArt art(fonts, &appearances);
  fonts.set_pixel_scale(1);
  const auto a = art.status_text("-37", 0xff0000U);
  check(a.image && a.image->pixel_scale == 1 && a.width > 0 && a.height > 0, "label rasterized");
  check(art.status_text("-37", 0xff0000U).image == a.image, "cache hit returns the same image");
  check(art.status_text("-37", 0x00ff00U).image != a.image &&
            art.status_text("-38", 0xff0000U).image != a.image,
        "color and text are part of the key");
  check(art.status_text("-37", 0xff000000U | 0xff0000U).image == a.image,
        "alpha byte of server colors is ignored");
  const auto sign = art.sign_text("Realm|of|the Mad God", 100);
  const auto sign_newlines = art.sign_text("Realm\nof\nthe Mad God", 100);
  const auto single_line = art.sign_text("Realm of the Mad God", 100);
  check(sign.image && sign.image->width > 0 && sign.height > single_line.height &&
            sign.image->pixels == sign_newlines.image->pixels,
        "sign text uses centered 24 px multiline font and maps vertical bars to line breaks");
  check(art.sign_text("Realm|of|the Mad God", 100).image == sign.image &&
            art.sign_text("Realm|of|the Mad God", 200).image != sign.image,
        "sign text is cached by text and object size");
  // Names are 16 pt, status text 24 pt: the same string is taller as status text.
  check(art.name("-37").height < a.height && art.name("-37").image != a.image,
        "names use their own size");
  fonts.set_pixel_scale(2);
  const auto b = art.status_text("-37", 0xff0000U);
  check(b.image != a.image && b.image->pixel_scale == 2 && std::abs(b.width - a.width) < 1e-9 &&
            b.image->width >= a.image->width * 2 - 8,
        "a new density re-rasterizes at the same logical size");
  fonts.set_pixel_scale(1);
  for (std::size_t i = 0; i < StatusArt::maximum_labels + 200; ++i) {
    (void)art.name("Player" + std::to_string(i));
  }
  check(art.entries() <= StatusArt::maximum_labels && art.bytes() <= StatusArt::maximum_bytes &&
            art.evictions() >= 200,
        "label cache is bounded");
  // Condition icons: 16x16, art centered on 8x8, black outline and glow around it, nothing at the
  // corners.
  const auto icon = art.icon(43);
  check(icon && icon->width == 16 && icon->height == 16 && art.icon(43) == icon,
        "icon is cached 16x16");
  const auto alpha = [&](int x, int y) {
    return icon->pixels[static_cast<std::size_t>((y * 16 + x) * 4 + 3)];
  };
  check(alpha(0, 0) == 0 && alpha(15, 15) == 0, "nothing reaches the corners");
  check(alpha(8, 8) == 255, "art pixels are opaque at the center");
  bool outlined = false;
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      if (alpha(x, y) > 0 && alpha(x, y) < 255) {
        outlined = true;
      }
    }
  }
  check(outlined, "outline and glow give partial alpha");
  check(alpha(8, 1) > 0 && alpha(8, 1) <= 77, "the 0.3 alpha glow reaches past the outline");
  check(art.icon(5000) != nullptr, "out-of-range frames are blank, not fatal");
}
}

int main() {
  try {
    FontBook fonts;
    conditions();
    bars();
    events();
    progress();
    texts(fonts);
    scatter();
    art_cache(fonts, realm::flash::source_root);
    std::cout << "status rules passed (" << checks << " checks)\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
