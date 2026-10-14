#pragma once
#include "realm/tutorial/Tutorial.hpp"
#include "realm/util/Image.hpp"
namespace realm {
[[nodiscard]] Image render_tutorial(std::span<const TutorialStroke> strokes, std::int32_t time);
}
