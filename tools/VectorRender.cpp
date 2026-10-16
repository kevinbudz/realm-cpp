// Renders a vector_art SVG to a straight-alpha PNG with the client's own rasterizer, for authoring
// and for tools/compare_deca_ui.py. Not a ctest.
// Usage: realm_vector_render INPUT.svg OUTPUT.png [WIDTH HEIGHT [SCALE]] [--flip-x] [--flip-y]
//        [--press] [--hover] [--grey]
// WIDTH and HEIGHT are design units (0 or omitted: the drawing's natural size); SCALE is pixels
// per unit (default 1). --press, --hover and --grey are the Deca button states.
#include "realm/assets/VectorArt.hpp"
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  std::vector<std::string> positional;
  realm::vector_art::Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--flip-x") {
      options.flip_x = true;
    } else if (argument == "--flip-y") {
      options.flip_y = true;
    } else if (argument == "--press") {
      options.content_scale = 0.9;
    } else if (argument == "--hover") {
      options.tint = std::pair{0x00ffffU, 0.1};
    } else if (argument == "--grey") {
      options.grey = true;
    } else {
      positional.push_back(argument);
    }
  }
  if (positional.size() < 2 || positional.size() > 5) {
    std::cerr << "usage: realm_vector_render INPUT.svg OUTPUT.png [WIDTH HEIGHT [SCALE]] "
                 "[--flip-x] [--flip-y] [--press] [--hover] [--grey]\n";
    return 2;
  }
  try {
    std::ifstream input(positional[0], std::ios::binary);
    if (!input) {
      throw std::runtime_error("cannot read " + positional[0]);
    }
    std::stringstream text;
    text << input.rdbuf();
    const auto drawing = realm::vector_art::parse(text.str());
    double width = positional.size() > 2 ? std::strtod(positional[2].c_str(), nullptr) : 0;
    double height = positional.size() > 3 ? std::strtod(positional[3].c_str(), nullptr) : 0;
    const double scale = positional.size() > 4 ? std::strtod(positional[4].c_str(), nullptr) : 1;
    width = width > 0 ? width : drawing.width;
    height = height > 0 ? height : drawing.height;
    auto image = realm::vector_art::render(drawing, width, height, scale, options);
    // Premultiplied to straight alpha for the PNG.
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
      const unsigned alpha = image.pixels[i + 3];
      for (std::size_t c = 0; c < 3 && alpha > 0; ++c) {
        image.pixels[i + c] = static_cast<unsigned char>(
            std::min(255U, (image.pixels[i + c] * 255U + alpha / 2) / alpha));
      }
    }
    realm::save_png(positional[1], image);
  } catch (const std::exception& error) {
    std::cerr << positional[0] << ": " << error.what() << '\n';
    return 1;
  }
  return 0;
}
