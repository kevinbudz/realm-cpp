#include "realm/tutorial/Tutorial.hpp"
#include "realm/tutorial/TutorialView.hpp"
#include "realm/assets/FlashDraw.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace realm;

void check(bool value, const char* reason) {
  if (!value) {
    throw std::runtime_error(reason);
  }
}

// FlashDraw's BlurFilter exactly as it was computed before the buffer-reuse rewrite: per-sample
// bounds checks, the same weights and summation order, and floor(sum + 1e-9). The new blur must
// match it byte for byte.
void reference_box(std::vector<double>& plane, int width, int height, double size,
                   bool horizontal) {
  const double radius = size / 2;
  if (radius <= .5) {
    return;
  }
  const int reach = static_cast<int>(std::ceil(radius - .5));
  std::vector<double> weights;
  for (int k = -reach; k <= reach; ++k) {
    weights.push_back(std::max(0.0, std::min(k + .5, radius) - std::max(k - .5, -radius)) / size);
  }
  const int lines = horizontal ? height : width, length = horizontal ? width : height;
  std::vector<double> source(static_cast<std::size_t>(length));
  for (int line = 0; line < lines; ++line) {
    const auto index = [&](int i) {
      return horizontal ? static_cast<std::size_t>(line) * static_cast<std::size_t>(width) +
                              static_cast<std::size_t>(i)
                        : static_cast<std::size_t>(i) * static_cast<std::size_t>(width) +
                              static_cast<std::size_t>(line);
    };
    for (int i = 0; i < length; ++i) {
      source[static_cast<std::size_t>(i)] = plane[index(i)];
    }
    for (int i = 0; i < length; ++i) {
      double sum{};
      for (int k = -reach; k <= reach; ++k) {
        if (i + k >= 0 && i + k < length) {
          sum += weights[static_cast<std::size_t>(k + reach)] *
                 source[static_cast<std::size_t>(i + k)];
        }
      }
      plane[index(i)] = std::floor(sum + 1e-9);
    }
  }
}

void reference_blur(Image& image, double blur_x, double blur_y, int quality) {
  const auto count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
  std::vector<double> plane(count);
  for (std::size_t c = 0; c < 4; ++c) {
    for (std::size_t i = 0; i < count; ++i) {
      plane[i] = image.pixels[i * 4 + c];
    }
    for (int pass = 0; pass < quality; ++pass) {
      reference_box(plane, image.width, image.height, blur_x, true);
      reference_box(plane, image.width, image.height, blur_y, false);
    }
    for (std::size_t i = 0; i < count; ++i) {
      image.pixels[i * 4 + c] =
          static_cast<unsigned char>(std::clamp(std::lround(plane[i]), 0L, 255L));
    }
  }
}

// The full-stage render that render_tutorial used before it was limited to the touched region. It
// is the byte-exact reference for render_tutorial_overlay.
Image reference_render(std::span<const TutorialStroke> strokes, std::int32_t time) {
  auto image = flash_draw::canvas(800, 600);
  auto back = flash_draw::canvas(800, 600);
  for (const auto& stroke : strokes) {
    flash_draw::Path path;
    for (std::size_t i = 0; i < stroke.points.size(); ++i) {
      const auto point = stroke.points[i];
      if (i == 0) {
        path.move(point.x, point.y);
      } else {
        path.line(point.x, point.y);
      }
    }
    flash_draw::stroke(back, path, stroke.width * 1.2, stroke.color);
    flash_draw::stroke(image, path, stroke.width, stroke.color);
  }
  const double blur = 5 + std::abs(std::sin(time / 300.0)) * 5;
  reference_blur(back, blur, blur, 1);
  flash_draw::draw(back, image, 0, 0);
  for (std::size_t i = 0; i < back.pixels.size(); i += 4) {
    const unsigned alpha = back.pixels[i + 3];
    for (std::size_t c = 0; c < 3; ++c) {
      back.pixels[i + c] = static_cast<unsigned char>(
          alpha ? std::min(255U, static_cast<unsigned>(back.pixels[i + c]) * 255 / alpha) : 0);
    }
  }
  return back;
}

// Fixed seed, so any failure reproduces.
class Rng {
 public:
  explicit Rng(std::uint32_t seed) : engine_(seed) {}

  double real(double low, double high) {
    return std::uniform_real_distribution<double>(low, high)(engine_);
  }

  int integer(int low, int high) { return std::uniform_int_distribution<int>(low, high)(engine_); }

  bool chance(double probability) { return real(0, 1) < probability; }

 private:
  std::mt19937 engine_;
};

// Points often sit on or just outside the stage edges, where clamping and clipping matter.
Vec2 random_point(Rng& rng) {
  static constexpr std::array<double, 7> edges_x{0, 1, 399.5, 799, 800, -3, 803};
  static constexpr std::array<double, 7> edges_y{0, 1, 299.5, 599, 600, -3, 603};
  const double x =
      rng.chance(.3) ? edges_x[static_cast<std::size_t>(rng.integer(0, 6))] : rng.real(-20, 820);
  const double y =
      rng.chance(.3) ? edges_y[static_cast<std::size_t>(rng.integer(0, 6))] : rng.real(-20, 620);
  return {x, y};
}

std::vector<TutorialStroke> random_strokes(Rng& rng) {
  std::vector<TutorialStroke> strokes;
  const int count = rng.integer(0, 3);
  for (int s = 0; s < count; ++s) {
    TutorialStroke stroke;
    stroke.width = rng.chance(.5) ? static_cast<double>(rng.integer(1, 5)) : rng.real(1, 5);
    stroke.color = static_cast<std::uint32_t>(rng.integer(0, 0xFFFFFF));
    const int points = rng.integer(0, 6);
    for (int p = 0; p < points; ++p) {
      stroke.points.push_back(random_point(rng));
    }
    strokes.push_back(std::move(stroke));
  }
  return strokes;
}

// Renders one frame through the region API and applies the returned rectangle to `surface` the way
// the UI surface does: a whole copy while it is empty, otherwise the rectangle's rows. The surface
// must equal the full-stage reference afterwards.
void expect_frame(std::span<const TutorialStroke> strokes, std::int32_t time, Image& surface) {
  const auto changed = render_tutorial_overlay(strokes, time);
  const auto& overlay = tutorial_overlay_image();
  check(changed.x >= 0 && changed.y >= 0 && changed.width >= 0 && changed.height >= 0 &&
            changed.x + changed.width <= 800 && changed.y + changed.height <= 600,
        "changed region lies on the stage");
  if (surface.pixels.empty()) {
    surface = overlay;
  } else {
    const auto length = static_cast<std::size_t>(changed.width) * 4;
    for (int y = changed.y; y < changed.y + changed.height; ++y) {
      const auto offset =
          (static_cast<std::size_t>(y) * 800 + static_cast<std::size_t>(changed.x)) * 4;
      std::copy_n(overlay.pixels.begin() + static_cast<std::ptrdiff_t>(offset), length,
                  surface.pixels.begin() + static_cast<std::ptrdiff_t>(offset));
    }
  }
  const auto want = reference_render(strokes, time);
  check(overlay.width == 800 && overlay.height == 600 && overlay.pixels == want.pixels,
        "overlay matches the full-stage render");
  check(surface.pixels == want.pixels, "uploaded region matches the full-stage render");
}

// Edges, a box around the minimap, an arrow, off-canvas and degenerate strokes, and a huge width.
std::vector<std::vector<TutorialStroke>> fixed_stroke_sets() {
  return {
      {{{{0, 0}, {800, 600}}, 1, 0xFF0000},
       {{{-30, 300}, {830, 300}}, 2, 0x00FF00},
       {{{400, -20}, {400, 620}}, 3, 0x0000FF},
       {{{795, 595}, {805, 605}, {900, 650}}, 5, 0xFFFFFF},
       {{{0, 599.5}, {799.5, 599.5}}, 1.5, 0x808080}},
      {{{{600, 10}, {790, 10}, {790, 190}, {600, 190}, {600, 10}}, 3, 0xFFD200},
       {{{600, 40}, {700, 40}, {700, 20}, {760, 50}, {700, 80}, {700, 60}, {600, 60}},
        4,
        0xFFD200}},
      {{{{900, 900}, {950, 950}}, 5, 0xFFFFFF},
       {{{-100, -100}, {-50, -50}}, 5, 0xFFFFFF},
       {{{400, 300}}, 4, 0xFFFFFF},
       {{}, 3, 0xFFFFFF},
       {{{100, 100}, {100, 100}, {120, 100}}, 2, 0x123456},
       {{{-10, 300}, {810, 300}}, 1, 0xABCDEF}},
      {{{{10, 10}, {20, 30}}, 60, 0x00FFFF}},
      {},
      {{{{0, 0}, {800, 600}}, 1, 0xFF0000}, {{{-30, 300}, {830, 300}}, 2, 0x00FF00}},
  };
}
}

// flash_draw::blur against the reference on random pixels: odd and tiny sizes, reach larger than
// the image, fractional blur sizes and several passes.
void check_blur_matches_reference() {
  Rng rng(7);
  const std::array<std::array<int, 2>, 7> sizes{
      {{64, 48}, {37, 53}, {1, 1}, {3, 40}, {40, 1}, {9, 7}, {120, 90}}};
  const std::array<double, 6> blurs{0.5, 1.5, 5, 7.3, 10, 12.7};
  for (const auto& size : sizes) {
    for (const double blur : blurs) {
      for (const int quality : {1, 2}) {
        Image random;
        random.width = size[0];
        random.height = size[1];
        random.pixels.resize(static_cast<std::size_t>(size[0]) * static_cast<std::size_t>(size[1]) *
                             4);
        for (auto& byte : random.pixels) {
          byte = static_cast<unsigned char>(rng.integer(0, 255));
        }
        Image actual = random;
        Image expected = random;
        flash_draw::blur(actual, blur, blur * .8 + .3, quality);
        reference_blur(expected, blur, blur * .8 + .3, quality);
        check(actual.pixels == expected.pixels, "flash_draw::blur matches the reference blur");
      }
    }
  }
}

int main() {
  try {
    check_blur_matches_reference();
    // The first render of the process uploads the whole stage, so the UI surface exists.
    const auto first = render_tutorial_overlay({}, 0);
    check(first.x == 0 && first.y == 0 && first.width == 800 && first.height == 600,
          "first render uploads the whole stage");

    Catalog catalog(flash::assets_root);
    Tutorial tutorial(catalog, flash::assets_root);
    World world(catalog);
    world.initialize(20, 20, "Tutorial");
    world.add(1, catalog.object("Wizard")->type, {5, 5});
    world.set_player_id(1);
    auto& sign = world.add(2, catalog.object("Sign")->type, {6, 5});
    sign.profile.name = "Check out the|minimap.";
    auto strokes = tutorial.update(world, 0);
    check(strokes.size() == 2, "near matching sign activates minimap box and arrow");
    check(strokes[0].points[0].x == 250 && strokes[0].points[0].y == 200,
          "box animates from Flash origin even when satisfaction begins at zero time");
    strokes = tutorial.update(world, 600);
    check(strokes[0].points[0].x >= 600 && strokes[0].points[1].x <= 800,
          "settled minimap box covers right HUD");
    const auto image = render_tutorial(strokes, 600);
    check(image.width == 800 && image.height == 600, "tutorial raster uses stage coordinates");
    check(
        std::any_of(image.pixels.begin(), image.pixels.end(), [](auto byte) { return byte != 0; }),
        "overlay raster contains visible highlights");
    bool translucent{};
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
      if (image.pixels[i + 3] > 0 && image.pixels[i + 3] < 255) {
        translucent = true;
        check(image.pixels[i + 1] > 200,
              "tutorial glow retains straight-alpha green for UI upload");
      }
    }
    check(translucent, "tutorial glow has translucent edges");
    world.entity(2)->profile.name = "Other sign";
    check(tutorial.update(world, 700).empty(), "sign text is part of the proximity requirement");
    world.entity(2)->profile.name = "Check out the|minimap.";
    world.entity(1)->motion.position = {12, 12};
    check(tutorial.update(world, 800).empty(), "leaving radius hides overlay");
    world.entity(1)->motion.position = {5, 5};
    strokes = tutorial.update(world, 900);
    check(strokes[0].points[0].x == 250, "reentering radius restarts animation");
    world.initialize(20, 20, "Nexus");
    check(tutorial.update(world, 1000).empty(), "map change clears tutorial overlays");
    world.initialize(20, 20, "Tutorial");
    check(tutorial.update(world, 1100).empty(), "missing player hides tutorial");

    // Region-limited output must equal the full-stage reference byte for byte, across a surface
    // that keeps state between frames (so moved and cleared highlights are checked too).
    auto sets = fixed_stroke_sets();
    // The stroke cache must notice a color change and a one-ulp point change with the same
    // geometry, so both follow the sets they resemble.
    auto recolored = sets[0];
    for (auto& stroke : recolored) {
      stroke.color ^= 0x010101U;
    }
    auto nudged = sets[1];
    nudged[0].points[1].x = std::nextafter(nudged[0].points[1].x, 1e9);
    sets.insert(sets.begin() + 1, recolored);
    sets.insert(sets.begin() + 3, nudged);
    const std::array<std::int32_t, 6> times{0, 300, 471, 900, 1234, 2999};
    std::size_t frames = 0;
    Image surface;
    for (const auto& set : sets) {
      for (const auto time : times) {
        expect_frame(set, time, surface);
        ++frames;
      }
    }
    Rng rng(20261018);
    for (int sequence = 0; sequence < 10; ++sequence) {
      Image random_surface;
      for (int frame = 0; frame < 8; ++frame) {
        const auto random_set = random_strokes(rng);
        expect_frame(random_set, rng.integer(0, 4000), random_surface);
        ++frames;
      }
    }
    std::cout << "Tutorial overlay matched the full-stage reference in " << frames << " frames\n";
    std::cout << "Tutorial proximity, animation, raster and lifecycle checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
