#pragma once
#include "realm/tutorial/Tutorial.hpp"
#include "realm/assets/Image.hpp"
#include <cstdint>
#include <span>

namespace realm {
// A rectangle of tutorial_overlay_image(), in stage pixels.
struct TutorialRegion {
  int x{};
  int y{};
  int width{};
  int height{};
};

// Redraws the persistent 800x600 straight-alpha overlay for these strokes and returns the
// rectangle whose pixels may have changed since the previous call (the whole stage on the first
// call). Upload that rectangle after every call, including calls with no strokes, or the surface
// keeps the cleared highlight.
[[nodiscard]] TutorialRegion render_tutorial_overlay(std::span<const TutorialStroke> strokes,
                                                     std::int32_t time);
[[nodiscard]] const Image& tutorial_overlay_image();
// A copy of the full overlay after rendering the strokes, for tests and tools.
[[nodiscard]] Image render_tutorial(std::span<const TutorialStroke> strokes, std::int32_t time);
}
