#include "realm/map/Camera.hpp"
#include "realm/objects/ProjectileProperties.hpp"
#include "realm/messaging/Protocol.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <string>

namespace {
int checks{};
void check(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
void close(double actual, double expected, const char* message, double tolerance = 1e-10) {
  check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
template <class F>
void rejected(F&& action, const char* message) {
  bool threw = false;
  try {
    action();
  } catch (const std::exception&) {
    threw = true;
  }
  check(threw, message);
}
void camera_tests() {
  realm::Camera c;
  c.configure({10, 20}, 0, 800, 600);
  close(c.clip_rect().x, -300, "native clip x");
  close(c.clip_rect().y, -325, "native centered clip y");
  const auto p = c.world_to_screen({10, 20});
  close(p.x, 300, "player screen x");
  close(p.y, 325, "player screen y");
  close(c.world_to_screen({11, 21}, 1).y, 325, "height subtracts from screen y");
  c.configure({10, 20}, std::numbers::pi / 2, 800, 600);
  const auto rotated = c.world_to_screen({11, 20});
  close(rotated.x, 300, "rotated x");
  close(rotated.y, 275, "rotated y");
  c.configure({10, 20}, 0, 1920, 1080, 2, false);
  close(c.playable_width(), 1560, "scaled HUD allowance");
  close(c.world_to_screen({10, 20}).x, 780, "resized camera center");
  close(c.world_to_screen({10, 20}).y, 810, "offset camera height");
  close(c.world_to_screen({11, 20}).x, 880, "zoom preserves HUD and doubles world scale");
  for (double angle : {-std::numbers::pi, -.13, 0.0, std::numbers::pi / 2, 5.8})
    for (double zoom : {.25, 1.0, 4.0}) {
      c.configure({100.1, -34.6}, angle, 1280, 720, zoom);
      const realm::Vec2 world{98.72, -38.33};
      const auto restored = c.screen_to_world(c.world_to_screen(world));
      close(restored.x, world.x, "world/screen inverse x");
      close(restored.y, world.y, "world/screen inverse y");
    }
  rejected([&] { c.configure({}, 0, 800, 600, 0); }, "reject zero zoom");
}
void projectile_tests() {
  realm::ProjectileProperties p;
  p.speed = 100;
  p.lifetime_ms = 1000;
  auto q = realm::projectile_position(p, {5, 7}, 0, 0, 250);
  close(q.x, 7.5, "straight distance");
  close(q.y, 7, "straight y");
  p.accelerate = true;
  close(realm::projectile_position(p, {}, 0, 0, 500).x, 2.5,
        "Flash acceleration convention (not integrated acceleration)");
  p.accelerate = false;
  p.decelerate = true;
  close(realm::projectile_position(p, {}, 0, 0, 500).x, 7.5, "deceleration convention");
  p.decelerate = false;
  p.boomerang = true;
  close(realm::projectile_position(p, {}, 0, 0, 500).x, 5, "boomerang apex");
  close(realm::projectile_position(p, {}, 0, 0, 750).x, 2.5, "boomerang return");
  close(realm::projectile_position(p, {}, 0, 0, 1000).x, 0, "boomerang origin");
  p.accelerate = true;
  close(realm::projectile_position(p, {}, 0, 0, 750).x, 4.375, "acceleration/boomerang uses base-speed halfway");
  p.accelerate = false;
  p.boomerang = false;
  p.amplitude = 2;
  p.frequency = 1;
  q = realm::projectile_position(p, {}, 0, 0, 250);
  close(q.x, 2.5, "sine distance");
  close(q.y, 2, "even bullet sine phase");
  close(realm::projectile_position(p, {}, 0, 1, 250).y, -2, "odd bullet sine phase");
  p.amplitude = 0;
  p.parametric = true;
  p.magnitude = 3;
  close(realm::projectile_position(p, {}, 0, 0, 250).x, -3, "even parametric phase");
  close(realm::projectile_position(p, {}, 0, 1, 250).x, 3, "odd parametric phase");
  close(realm::projectile_position(p, {}, 0, 0, 125).y, 3, "parametric second harmonic");
  close(realm::projectile_position(p, {}, 0, 2, 125).y, -3, "parametric quadrant phase");
  close(realm::projectile_position(p, {}, 0, -1, 125).y, 3, "negative bullet preserves AS3 signed remainder");
  close(realm::projectile_position(p, {}, 0, -2, 125).y, 3, "negative even bullet preserves AS3 signed remainder");
  p.parametric = false;
  p.wavy = true;
  q = realm::projectile_position(p, {}, 0, 0, 250);
  close(q.x, 2.496988640512931, "wavy numeric reference", 1e-9);
  close(q.y, -0.12266918581854504, "wavy numeric reference y", 1e-9);
  p.lifetime_ms = 0;
  rejected([&] { (void)realm::projectile_position(p, {}, 0, 0, 1); }, "reject zero projectile lifetime");
}
void random_tests() {
  realm::FlashRandom r(1);
  for (auto expected : {16807U, 282475249U, 1622650073U, 984943658U, 1144108930U})
    check(r.next() == expected, "Park Miller known sequence");
  realm::FlashRandom zero(0);
  check(zero.next() == 0, "preserve Flash zero seed");
  realm::FlashRandom modulus(2147483647U);
  check(modulus.next() == 2147483647U, "preserve Flash modulus seed");
  // Extreme uint seeds are a different path from a signed Park Miller implementation.
  realm::FlashRandom extreme(0xffffffffU);
  check(extreme.next() == 16807U, "AS3 uint wraparound seed");
  realm::FlashRandom ranges(1);
  check(ranges.range(5, 5) == 5 && ranges.seed() == 1, "equal range consumes no RNG");
  ranges.drop(3);
  check(ranges.seed() == 1622650073U, "drop sequence");
}
void codec_tests() {
  using namespace realm::net;
  const Bytes golden{0, 0, 0, 17, 4, 0, 0, 3, 232, 0x3f, 0xc0, 0, 0, 0xc0, 0x10, 0, 0};
  check(frame(realm::flash::PacketId::MOVE, move_body(1000, {1.5F, -2.25F})) == golden, "MOVE big-endian wire fixture");
  for (std::size_t split = 0; split <= golden.size(); ++split) {
    Framer f;
    auto first = f.feed(std::span(golden).first(split));
    auto second = f.feed(std::span(golden).subspan(split));
    check(first.size() + second.size() == 1, "TCP split yields exactly one packet");
    const auto& packet = first.empty() ? second.front() : first.front();
    check(packet.id == 4 && packet.body == Bytes(golden.begin() + 5, golden.end()), "TCP split preserves body");
  }
  Framer f;
  std::size_t packets{};
  for (auto byte : golden) {
    const std::array<std::uint8_t, 1> b{byte};
    packets += f.feed(b).size();
  }
  check(packets == 1 && f.pending_bytes() == 0, "one-byte TCP fragments");
  Bytes combined = golden;
  const auto empty = frame(realm::flash::PacketId::ESCAPE, {});
  combined.insert(combined.end(), empty.begin(), empty.end());
  auto parsed = f.feed(combined);
  check(parsed.size() == 2 && parsed.back().body.empty(), "coalesced packets and empty body");
  for (const Bytes& bad : {Bytes{0, 0, 0, 0}, Bytes{0, 0, 0, 4}, Bytes{0, 0x10, 0, 1}, Bytes{0xff, 0xff, 0xff, 0xff}}) {
    rejected([&] { (void)f.feed(bad); }, "reject invalid frame length");
    check(f.pending_bytes() == 0, "framer recovers after malformed length");
  }
  const Bytes maximum(max_packet_length - 5, 0x42);
  auto max_frames = f.feed(frame(9, maximum));
  check(max_frames.size() == 1 && max_frames[0].body == maximum, "maximum legal frame");
  Writer w;
  w.utf("caf\xc3\xa9");
  Reader r(w.bytes());
  check(r.utf() == "caf\xc3\xa9", "writeUTF byte length for non-ASCII");
  r.finish();
  rejected(
      [&] {
        Writer too_long;
        too_long.utf(std::string(65536, 'a'));
      },
      "UTF length overflow");
  // Signed tile coordinates, HP, numeric account ID, UTF player name, and an exploding drop.
  const Bytes update_fixture{0,    1,    0xff, 0xff, 0,    2,    0x12, 0x34, 0,   1, 0x03, 0x0e, 0, 0,   0,  42,
                             0x3f, 0xc0, 0,    0,    0xc0, 0x10, 0,    0,    3,   1, 0,    0,    0, 100, 35, 0,
                             0,    0,    9,    28,   0,    3,    'B',  'o',  'b', 0, 1,    0,    0, 0,   77, 1};
  auto u = parse_update(update_fixture);
  check(u.tiles.size() == 1 && u.tiles[0].x == -1 && u.tiles[0].type == 0x1234, "UPDATE signed tile fixture");
  check(u.objects.size() == 1 && u.objects[0].status.id == 42, "UPDATE object fixture");
  check(std::get<std::int32_t>(u.objects[0].status.stats[1].value) == 9, "account stat is numeric in this fork");
  check(std::get<std::string>(u.objects[0].status.stats[2].value) == "Bob", "name string stat fixture");
  check(u.drops.size() == 1 && u.drops[0].id == 77 && u.drops[0].explode, "exploding drop field");
  for (std::size_t n = 0; n < update_fixture.size(); ++n)
    rejected([&] { (void)parse_update(std::span(update_fixture).first(n)); }, "reject each truncated UPDATE prefix");
  const Bytes tick_empty{0, 0};
  check(parse_tick(tick_empty).player_stats.empty(), "NEWTICK missing optional tail");
  const Bytes tick_stats{0, 0, 1, 56, 0, 1, 'G'};
  check(std::get<std::string>(parse_tick(tick_stats).player_stats[0].value) == "G",
        "NEWTICK optional player stat tail");
  const Bytes negative_count{0xff, 0xff};
  rejected([&] { (void)parse_tick(negative_count); }, "negative collection count");
  const Bytes map_fixture{0, 0, 0, 32, 0, 0, 0, 64, 0, 1, 'N', 0, 1, 'D', 0, 0, 0, 1, 0xff, 0xff, 0xff, 0xff, 1, 0};
  const auto map = parse_map_info(map_fixture);
  check(map.width == 32 && map.height == 64 && map.background == -1 && map.show_displays && !map.allow_teleport,
        "MAPINFO field order fixture");
}
}
int main() {
  try {
    camera_tests();
    projectile_tests();
    random_tests();
    codec_tests();
    std::cout << checks << " parity and protocol checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
