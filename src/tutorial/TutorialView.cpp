#include "realm/tutorial/TutorialView.hpp"
#include "realm/util/FlashDraw.hpp"
#include <cmath>
#include <algorithm>
namespace realm {
Image render_tutorial(std::span<const TutorialStroke> strokes, std::int32_t time) {
  auto image = flash_draw::canvas(800, 600);
  auto back = flash_draw::canvas(800, 600);
  for (const auto& stroke : strokes) {
    flash_draw::Path path;
    for (std::size_t i = 0; i < stroke.points.size(); ++i) {
      const auto point = stroke.points[i];
      if (i == 0)
        path.move(point.x, point.y);
      else
        path.line(point.x, point.y);
    }
    flash_draw::stroke(back, path, stroke.width * 1.2, stroke.color);
    flash_draw::stroke(image, path, stroke.width, stroke.color);
  }
  const double blur = 5 + std::abs(std::sin(time / 300.0)) * 5;
  flash_draw::blur(back, blur, blur);
  flash_draw::draw(back, image, 0, 0);
  for (std::size_t i = 0; i < back.pixels.size(); i += 4) {
    const unsigned alpha = back.pixels[i + 3];
    for (std::size_t c = 0; c < 3; ++c)
      back.pixels[i + c] = static_cast<unsigned char>(
          alpha ? std::min(255U, static_cast<unsigned>(back.pixels[i + c]) * 255 / alpha) : 0);
  }
  return back;
}
}
