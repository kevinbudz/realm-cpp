#include <GL/glew.h>
#include "realm/background/Backdrop.hpp"
#include "realm/map/PostEffects.hpp"
#include "realm/stage3D/Renderer.hpp"
#include "realm/map/World.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

// Environment/post effects: the pure rules from Map.as, the star and water
// backgrounds, and the real Renderer's offscreen filter, breath vignette and sidebar strip.
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

using Pixels = std::vector<unsigned char>;

struct Frame {
  int width, height;
  Pixels pixels;

  std::array<int, 3> at(int x, int y) const {
    const auto* p = &pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) *
                            4];
    return {p[0], p[1], p[2]};
  }
};

Frame render(Renderer& renderer, const Atlas& atlas, std::vector<Quad> world, int width, int height,
             int clip, const ScreenEffects& fx, std::vector<Quad> overlay = {}) {
  renderer.draw(atlas, world, width, height, clip, overlay, fx);
  auto image = renderer.capture(width, height);
  check(glGetError() == GL_NO_ERROR, "GL error after draw");
  return {width, height, std::move(image.pixels)};
}

Quad solid(double x, double y, double w, double h, std::array<float, 4> color) {
  Quad q;
  q.origin = {static_cast<float>(x), static_cast<float>(y)};
  q.axis_x = {static_cast<float>(w), 0};
  q.axis_y = {0, static_cast<float>(h)};
  q.kind = 2;
  q.multiply = color;
  return q;
}

void expect(const std::array<int, 3>& actual, std::array<double, 3> expected, double tolerance,
            const std::string& why) {
  for (int i = 0; i < 3; ++i) {
    near(actual[static_cast<std::size_t>(i)], expected[static_cast<std::size_t>(i)], tolerance,
         why + " channel " + std::to_string(i));
  }
}

void rendering() {
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    throw std::runtime_error(SDL_GetError());
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  auto* window = SDL_CreateWindow("postfx", 0, 0, 800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!window) {
    throw std::runtime_error(SDL_GetError());
  }
  auto context = SDL_GL_CreateContext(window);
  if (!context) {
    throw std::runtime_error(SDL_GetError());
  }
  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) {
    throw std::runtime_error("GLEW");
  }
  while (glGetError() != GL_NO_ERROR) {
  }
  {
    Atlas atlas(flash::source_root);
    Renderer renderer(64);
    const std::array<float, 4> orange{.8F, .4F, .2F, 1}, white{1, 1, 1, 1};
    const auto field = [&](double w, double h) { return solid(0, 0, w, h, orange); };
    // No effects: the plain path, with the area right of the clip left black.
    auto f = render(renderer, atlas, {field(600, 600)}, 800, 600, 600, {});
    expect(f.at(100, 100), {204, 102, 51}, 1, "plain world");
    expect(f.at(700, 300), {0, 0, 0}, 0, "outside the world");
    // Blind: every channel is 0.05 * (r + g + b); the area right of the map and the overlay stay
    // unfiltered.
    ScreenEffects fx;
    fx.blind = true;
    f = render(renderer, atlas, {field(600, 600)}, 800, 600, 600, fx,
               {solid(650, 100, 100, 100, {1, 0, 0, 1})});
    expect(f.at(100, 100), {.07 * 255, .07 * 255, .07 * 255}, 1.5, "blind gray");
    expect(f.at(700, 300), {0, 0, 0}, 0, "blind leaves the HUD column");
    expect(f.at(700, 150), {255, 0, 0}, 0, "overlay is not filtered");
    // Blur: a 20 px box. A white square's edge is half covered (10.5 of 20 taps), and the map edge
    // fades to the black behind it.
    fx = {};
    fx.blur = 20;
    f = render(renderer, atlas, {solid(250, 250, 100, 100, white)}, 800, 600, 600, fx);
    expect(f.at(300, 300), {255, 255, 255}, 1, "blur keeps a wide interior");
    expect(f.at(250, 300), {.525 * 255, .525 * 255, .525 * 255}, 4, "blur halves the square edge");
    expect(f.at(100, 300), {0, 0, 0}, 0, "blur far away");
    expect(f.at(200, 300), {0, 0, 0}, 1, "blur reach is 10 px");
    f = render(renderer, atlas, {field(600, 600)}, 800, 600, 600, fx);
    expect(f.at(0, 300), {.525 * 204, .525 * 102, .525 * 51}, 4, "map edge fades toward black");
    expect(f.at(300, 300), {204, 102, 51}, 1, "blur keeps flat color");
    expect(f.at(650, 300), {0, 0, 0}, 0, "blur stays inside the map");
    // Drunk plus blind: blur first, then the color matrix (they commute: both linear).
    fx.blind = true;
    f = render(renderer, atlas, {field(600, 600)}, 800, 600, 600, fx);
    expect(f.at(300, 300), {.07 * 255, .07 * 255, .07 * 255}, 1.5, "blur and blind together");
    // Breath vignette: clear in the middle, tinted toward (255,55,0) at the corners by 0.92 *
    // alpha.
    fx = {};
    fx.vignette = .5F;
    f = render(renderer, atlas, {solid(0, 0, 600, 600, {.2F, .2F, .2F, 1})}, 800, 600, 600, fx);
    expect(f.at(300, 300), {51, 51, 51}, 1, "vignette is clear in the center");
    for (const auto [x, y] : std::array<std::array<int, 2>, 3>{{{5, 5}, {590, 40}, {150, 580}}}) {
      const double r = std::hypot((x + .5 - 300) / (600 / std::sqrt(2.0)),
                                  (y + .5 - 300) / (600 / std::sqrt(2.0)));
      const double a = vignette_edge_alpha(static_cast<float>(r)) * .5;
      expect(f.at(x, y), {51 * (1 - a) + 255 * a, 51 * (1 - a) + 55 * a, 51 * (1 - a)}, 3,
             "vignette at " + std::to_string(x) + "," + std::to_string(y));
    }
    // Sidebar strip: black at the map's right edge, 0 -> 1 across the last 10 px.
    fx = {};
    fx.sidebar = 10;
    f = render(renderer, atlas, {solid(0, 0, 600, 600, white)}, 800, 600, 600, fx);
    expect(f.at(589, 300), {255, 255, 255}, 0, "strip starts at width - 10");
    expect(f.at(594, 300), {255 * (1 - .45), 255 * (1 - .45), 255 * (1 - .45)}, 3,
           "strip midpoint");
    expect(f.at(599, 300), {255 * .05, 255 * .05, 255 * .05}, 3, "strip ends black");
    expect(f.at(700, 300), {0, 0, 0}, 0, "nothing right of the strip");
    // Resize: the offscreen target follows the drawable size and the effects scale with it.
    fx = {};
    fx.blind = true;
    fx.blur = 30;
    fx.vignette = .3F;
    fx.sidebar = 15;
    f = render(renderer, atlas, {solid(0, 0, 900, 900, orange)}, 1200, 900, 900, fx);
    expect(f.at(450, 450), {.07 * 255, .07 * 255, .07 * 255}, 2, "resized frame");
    f = render(renderer, atlas, {field(600, 600)}, 800, 600, 600, {});
    expect(f.at(100, 100), {204, 102, 51}, 1, "back to the plain path after a resize");
    // Backgrounds draw behind everything and deterministically.
    Camera camera;
    camera.configure({50, 50}, .7, 800, 600, 1, true);
    for (const int kind : {1, 2}) {
      Backdrop backdrop;
      backdrop.reset(kind, 9);
      std::vector<Quad> quads;
      backdrop.build(camera, atlas, 1234, quads);
      check(!quads.empty(), "backdrop emits quads");
      if (kind == 2) {
        for (const auto& q : quads) {
          check(q.kind == 3 && q.texture != 0, "water tiles are repeat-path surface quads");
        }
      }
      const auto first = render(renderer, atlas, quads, 800, 600, 800, {});
      std::vector<Quad> again;
      backdrop.build(camera, atlas, 1234, again);
      check(render(renderer, atlas, again, 800, 600, 800, {}).pixels == first.pixels,
            "backdrop draws identically");
      bool lit = false;
      for (int y = 0; y < 600 && !lit; y += 3) {
        for (int x = 0; x < 800 && !lit; x += 3) {
          const auto p = first.at(x, y);
          lit = p[0] | p[1] | p[2];
        }
      }
      check(lit, "backdrop paints visible pixels");
    }
    {
      // Water moves 0.01 px/ms diagonally: after 1000 ms the field is shifted by (10, 10) px at
      // angle 0.
      Camera flat;
      flat.configure({50, 50}, 0, 800, 600, 1, true);
      Backdrop water;
      water.reset(2, 3);
      std::vector<Quad> a, b;
      water.build(flat, atlas, 0, a);
      water.build(flat, atlas, 1000, b);
      const auto fa = render(renderer, atlas, a, 800, 600, 800, {}),
                 fb = render(renderer, atlas, b, 800, 600, 800, {});
      int equal = 0, total = 0;
      for (int y = 40; y < 500; y += 7) {
        for (int x = 40; x < 700; x += 7) {
          ++total;
          equal += fa.at(x, y) == fb.at(x + 10, y + 10);
        }
      }
      check(equal >= total * .995, "water drifts by 10 px per second on both axes");
    }
  }
  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
}
}

int main() {
  try {
    rendering();
    std::cout << "Post effect checks: " << checks << " passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Post effect test failed: " << e.what() << "\n";
    return 1;
  }
}
