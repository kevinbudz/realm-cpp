// vector_art: the SVG subset, scale-9 stretching and the Deca UI art in ui/deca. Checks:
// - stretch(): fixed parts keep their size, only the band grows; narrower than the fixed parts,
//   everything shrinks uniformly and the band collapses;
// - render(): exact fills at a non-unit scale; a stretched outline keeps its width and its fixed
//   columns stay pixel-identical to the natural render; flip-x mirrors; grey is neutral;
// - parse(): unsupported constructs (text, image, use, radial gradients, filters) are rejected;
// - every ui/deca/*.svg parses with a whole-unit natural size and a 1-unit stretch band inside it,
//   renders non-empty at 2x and stretched, is embedded unchanged, and every paint(deca NAME) in
//   ui/deca.rcss names one; the grey generic_green_button renders neutral.
#include "realm/assets/VectorArt.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using namespace realm;
namespace va = realm::vector_art;
int checks{};

void check(bool ok, const std::string& why) {
  ++checks;
  if (!ok) {
    throw std::runtime_error(why);
  }
}

bool near(double a, double b) {
  return std::abs(a - b) < 1e-9;
}

std::string read(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read " + path.string());
  }
  std::stringstream text;
  text << input.rdbuf();
  return text.str();
}

const unsigned char* at(const Image& image, int x, int y) {
  return &image.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                        static_cast<std::size_t>(x)) *
                       4];
}

bool same_pixel(const Image& a, int ax, int ay, const Image& b, int bx, int by) {
  const auto* p = at(a, ax, ay);
  const auto* q = at(b, bx, by);
  return p[0] == q[0] && p[1] == q[1] && p[2] == q[2] && p[3] == q[3];
}

void stretch_rules() {
  const va::Stretch band{9, 10};
  check(near(va::stretch(5, 20, 40, band), 5), "left of the band keeps its position");
  check(near(va::stretch(9, 20, 40, band), 9), "the band starts in place");
  check(near(va::stretch(9.5, 20, 40, band), 19.5), "the band grows evenly");
  check(near(va::stretch(10, 20, 40, band), 30), "the band ends where the extra width ends");
  check(near(va::stretch(20, 20, 40, band), 40), "right of the band moves by the extra width");
  check(near(va::stretch(9, 20, 9.5, band), 4.5) && near(va::stretch(10, 20, 9.5, band), 4.5),
        "narrower than the fixed parts the band collapses");
  check(near(va::stretch(20, 20, 9.5, band), 9.5), "and the fixed parts shrink uniformly");
  check(near(va::stretch(5, 20, 40, std::nullopt), 10), "without a band the axis scales");
}

void render_rules() {
  const auto square = va::parse(
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4"><rect width="4" height="4" fill="#ff0000"/></svg>)");
  const auto filled = va::render(square, 4, 4, 2);
  check(filled.width == 8 && filled.height == 8, "4 units at scale 2 are 8 px");
  bool exact = true;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      const auto* p = at(filled, x, y);
      exact = exact && p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255;
    }
  }
  check(exact, "a rect fills every pixel exactly");

  // A 1-unit outline that stretches along x between 4 and 5.
  const auto frame = va::parse(
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10" data-stretch-x="4 5">)"
      R"(<path fill="#ffffff" fill-rule="evenodd" d="M0 0 H10 V10 H0 Z M1 1 V9 H9 V1 Z"/></svg>)");
  check(frame.stretch_x && near(frame.stretch_x->start, 4) && near(frame.stretch_x->end, 5) &&
            !frame.stretch_y,
        "data-stretch-x is read");
  const auto natural = va::render(frame, 10, 10, 2);
  const auto wide = va::render(frame, 30, 10, 2);
  check(wide.width == 60 && wide.height == 20, "stretched size");
  bool fixed = true;
  for (int y = 0; y < 20; ++y) {
    for (int x = 0; x < 8; ++x) {
      fixed = fixed && same_pixel(wide, x, y, natural, x, y) &&
              same_pixel(wide, 52 + x, y, natural, 12 + x, y);
    }
  }
  check(fixed, "columns outside the band are identical to the natural render");
  bool outline = true;
  for (int x = 8; x < 52; ++x) {
    outline = outline && at(wide, x, 0)[3] == 255 && at(wide, x, 1)[3] == 255 &&
              at(wide, x, 2)[3] == 0 && at(wide, x, 17)[3] == 0 && at(wide, x, 18)[3] == 255;
  }
  check(outline, "the outline keeps its 1-unit width across the band");

  const auto half = va::parse(
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="4" height="2"><rect width="2" height="2" fill="#00ff00"/></svg>)");
  va::Options flip;
  flip.flip_x = true;
  const auto plain = va::render(half, 4, 2, 1);
  const auto mirrored = va::render(half, 4, 2, 1, flip);
  check(at(plain, 0, 0)[3] == 255 && at(plain, 3, 0)[3] == 0 && at(mirrored, 0, 0)[3] == 0 &&
            at(mirrored, 3, 0)[3] == 255,
        "flip-x mirrors the drawing");
  va::Options grey;
  grey.grey = true;
  const auto greyed = va::render(half, 4, 2, 1, grey);
  const auto* g = at(greyed, 0, 0);
  check(g[0] == g[1] && g[1] == g[2] && g[0] > 150 && g[3] == 255, "grey is neutral luminance");
}

void rejections() {
  for (
      const char* body : {
          R"svg(<text x="0" y="0">hi</text>)svg",
          R"svg(<image href="a.png" width="1" height="1"/>)svg",
          R"svg(<use href="#a"/>)svg",
          R"svg(<defs><radialGradient id="r"/></defs><rect width="1" height="1" fill="url(#r)"/>)svg",
          R"svg(<rect width="1" height="1" filter="url(#f)"/>)svg",
          R"svg(<path d="M0 0 L1 1 Q"/>)svg",
      }) {
    bool rejected = false;
    try {
      (void)va::parse(
          std::string(R"(<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1">)") + body +
          "</svg>");
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    check(rejected, std::string("rejected: ") + body);
  }
}

void deca_art(const std::filesystem::path& ui) {
  std::set<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(ui / "deca")) {
    if (entry.path().extension() != ".svg") {
      continue;
    }
    const auto name = entry.path().stem().string();
    names.insert(name);
    const auto* embedded = va::embedded_deca_svg(entry.path().filename().string());
    check(embedded && *embedded == read(entry.path()), name + ": embedded copy matches ui/deca");
    va::Drawing drawing;
    try {
      drawing = va::parse(read(entry.path()));
    } catch (const std::exception& error) {
      check(false, name + ": " + error.what());
    }
    check(drawing.width >= 1 && drawing.height >= 1 &&
              near(drawing.width, std::round(drawing.width)) &&
              near(drawing.height, std::round(drawing.height)),
          name + ": whole-unit natural size");
    for (const auto& [band, size] : {std::pair{drawing.stretch_x, drawing.width},
                                     std::pair{drawing.stretch_y, drawing.height}}) {
      check(!band || (near(band->end - band->start, 1) && band->start > 0 && band->end < size),
            name + ": a 1-unit stretch band inside the drawing");
    }
    for (const auto& [w, h] : {std::pair{drawing.width, drawing.height},
                               std::pair{drawing.width + (drawing.stretch_x ? 37 : 0),
                                         drawing.height + (drawing.stretch_y ? 23 : 0)}}) {
      const auto image = va::render(drawing, w, h, 2);
      std::size_t covered = 0;
      for (std::size_t i = 3; i < image.pixels.size(); i += 4) {
        covered += image.pixels[i] > 0;
      }
      check(image.width == static_cast<int>(std::ceil(w * 2)) && covered > image.pixels.size() / 40,
            name + ": renders at " + std::to_string(w) + " x " + std::to_string(h));
    }
  }
  check(names.size() >= 40, "ui/deca holds the kit");
  check(!va::embedded_deca_svg("no_such_art.svg"), "unknown Deca art is not embedded");
  // HudArt::main_button draws generic_green_button grey, so its greyed pixels must be neutral.
  va::Options grey;
  grey.grey = true;
  const auto greyed =
      va::render(va::parse(read(ui / "deca" / "generic_green_button.svg")), 52, 35, 2, grey);
  bool neutral = true;
  for (std::size_t i = 0; i < greyed.pixels.size(); i += 4) {
    neutral = neutral && greyed.pixels[i] == greyed.pixels[i + 1] &&
              greyed.pixels[i + 1] == greyed.pixels[i + 2];
  }
  check(neutral, "the grey generic_green_button is neutral");
  const auto rcss = read(ui / "deca.rcss");
  const std::regex paint(R"(paint\(deca ([a-z][A-Za-z_]*))");
  std::size_t uses = 0;
  for (std::sregex_iterator it(rcss.begin(), rcss.end(), paint), end; it != end; ++it) {
    ++uses;
    check(names.count((*it)[1].str()) == 1, "deca.rcss names missing art " + (*it)[1].str());
  }
  check(uses >= 40, "deca.rcss paints the kit");
}
}

int main() {
  try {
    stretch_rules();
    render_rules();
    rejections();
    deca_art(std::filesystem::path(REALM_SOURCE_DIR) / "ui");
    std::cout << "vector art: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
