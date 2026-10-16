#include "realm/util/Telemetry.hpp"
#include "realm/assets/FontBook.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <bit>
#include <cmath>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

namespace realm {
namespace {
// Light hinting fits stems and x-heights to the vertical pixel grid only, so horizontal advances
// and sub-pixel pen positions are unchanged while horizontal strokes render sharp.
constexpr FT_Int32 glyph_load_flags = FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT;

// Vertical phases lie on the twentieth-pixel (twips) grid.
int style(int size, bool bold, bool shadow, double vertical_phase = 0, bool cached = false) {
  const auto twips = std::lround(vertical_phase * 20);
  if (!(vertical_phase >= 0 && vertical_phase < 1) ||
      std::abs(vertical_phase * 20 - static_cast<double>(twips)) > 1e-6) {
    throw std::runtime_error("Unsupported text vertical phase");
  }
  return (size * 4 + (bold ? 2 : 0) + (shadow ? 1 : 0)) * 20 + static_cast<int>(twips) +
         (cached ? 100000 : 0);
}

// SimpleText glyph coverage is the union of glyph paths: edge coverage is
// added before quantization instead of painting one bitmap over another.
void composite(Image& target, const Image& bitmap, int gx, int gy, int padding, double right,
               double bottom, std::uint32_t color) {
  for (int y = 0; y < bitmap.height; ++y) {
    for (int x = 0; x < bitmap.width; ++x) {
      const int tx = gx + x, ty = gy + y;
      if (tx < 0 || tx >= right || ty < 0 || ty >= bottom) {
        continue;
      }
      const auto src = (static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap.width) +
                        static_cast<std::size_t>(x)) *
                       4;
      const auto dst =
          (static_cast<std::size_t>(ty + padding) * static_cast<std::size_t>(target.width) +
           static_cast<std::size_t>(tx + padding)) *
          4;
      const unsigned clip =
          static_cast<unsigned>(std::lround(std::clamp(right - tx, 0.0, 1.0) * 4)) * 64U;
      const unsigned alpha = std::min<unsigned>(bitmap.pixels[src + 3], clip ? clip - 1 : 0);
      if (!alpha) {
        continue;
      }
      const unsigned previous_alpha = target.pixels[dst + 3];
      const auto combined = static_cast<unsigned char>(
          std::min(255U, alpha + previous_alpha + (previous_alpha ? 1U : 0U)));
      target.pixels[dst + 3] = combined;
      for (unsigned c = 0; c < 3; ++c) {
        target.pixels[dst + c] =
            static_cast<unsigned char>(((color >> (16U - 8U * c)) & 255U) * (combined + 1U) >> 8U);
      }
    }
  }
}

// Text effects act on the complete clipped TextField: a crisp black outline (see ui_outline_alpha)
// replaces Flash's blurred drop shadow, so glyph edges stay sharp against any background.
void finish(Image& image, bool outline, double radius = 1) {
  if (outline) {
    ui_outline_alpha(image, radius);
  }
  // Convert the internal premultiplied bytes to their PNG representation.
  for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
    const unsigned alpha = image.pixels[i + 3];
    if (!alpha || alpha == 255) {
      continue;
    }
    for (unsigned c = 0; c < 3; ++c) {
      image.pixels[i + c] = static_cast<unsigned char>(
          std::min(255U, (static_cast<unsigned>(image.pixels[i + c]) << 8U) / alpha));
    }
  }
}

std::string_view trailing_trimmed(std::string_view line) {
  while (!line.empty() && line.back() == ' ') {
    line.remove_suffix(1);
  }
  return line;
}

std::uint32_t character(std::string_view input, std::size_t& at) {
  const auto first = static_cast<unsigned char>(input[at++]);
  if (first < 128) {
    return first;
  }
  const int extra = first >= 0xc2 && first <= 0xdf   ? 1
                    : first >= 0xe0 && first <= 0xef ? 2
                    : first >= 0xf0 && first <= 0xf4 ? 3
                                                     : 0;
  if (!extra) {
    return '?';
  }
  std::uint32_t code = first & ((1U << (6 - extra)) - 1U);
  for (int i = 0; i < extra; ++i) {
    if (at == input.size() || (static_cast<unsigned char>(input[at]) & 0xc0U) != 0x80U) {
      return '?';
    }
    code = (code << 6U) | (static_cast<unsigned char>(input[at++]) & 0x3fU);
  }
  if (code > 0x10ffffU || (code >= 0xd800U && code <= 0xdfffU) || (extra == 1 && code < 128) ||
      (extra == 2 && code < 2048) || (extra == 3 && code < 65536)) {
    return '?';
  }
  return code;
}
}

struct FontBook::NativeFonts {
  FT_Library library{};
  std::array<FT_Face, 2> faces{};

  ~NativeFonts() {
    for (auto face : faces) {
      if (face) {
        FT_Done_Face(face);
      }
    }
    if (library) {
      FT_Done_FreeType(library);
    }
  }
};

FontBook::FontBook() : FontBook(flash::source_root) {}

FontBook::FontBook(const std::filesystem::path& root) : native_(std::make_unique<NativeFonts>()) {
  if (FT_Init_FreeType(&native_->library)) {
    throw std::runtime_error("Cannot initialize native font renderer");
  }
  for (std::size_t i = 0; i < 2; ++i) {
    const auto path =
        root / "src/com/company/ui/fonts" / (i ? "MyriadProBold.otf" : "MyriadPro.otf");
    if (FT_New_Face(native_->library, path.string().c_str(), 0, &native_->faces[i])) {
      throw std::runtime_error("Cannot load original Myriad font: " + path.string());
    }
    if (!FT_Get_Sfnt_Table(native_->faces[i], FT_SFNT_OS2)) {
      throw std::runtime_error("Original Myriad font has no OS/2 metrics: " + path.string());
    }
  }
}

FontBook::~FontBook() = default;

void FontBook::set_pixel_scale(double scale) {
  if (!std::isfinite(scale) || scale <= 0 || scale > 16) {
    throw std::invalid_argument("Invalid font pixel density");
  }
  pixel_scale_ = scale;
}

TextBitmap FontBook::rasterize_at_scale(std::string_view text, int size, double scale, bool bold,
                                        bool shadow, std::uint32_t color, Vec2 pixel_phase) const {
  if (!std::isfinite(scale) || scale <= 0 || scale > 16 || !std::isfinite(pixel_phase.x) ||
      !std::isfinite(pixel_phase.y) || pixel_phase.x < 0 || pixel_phase.x >= 1 ||
      pixel_phase.y < 0 || pixel_phase.y >= 1) {
    throw std::invalid_argument("Invalid animated font density or phase");
  }
  return rasterize_native(text, size, bold, shadow, color, 0, 0, 1, 0, 0, scale, pixel_phase);
}

TextBitmap FontBook::rasterize_glow(std::string_view text, int size, bool bold, std::uint32_t color,
                                    unsigned, double blur) const {
  if (!std::isfinite(blur) || blur <= 0 || blur > 16) {
    throw std::invalid_argument("Invalid outline blur");
  }
  const double scale = pixel_scale_;
  auto render = [this, text = std::string(text), size, bold, color, blur, scale](Vec2 phase) {
    // The outline reaches blur/2 logical pixels (plus an antialiased rim) past the ink;
    // padding=ceil(blur) keeps that reach inside the bitmap.
    const double padding = std::ceil(blur);
    // The glyph origin sits padding+phase inside the bitmap: whole pixels placed by the blit,
    // remainder by the glyphs.
    const double dx = padding * scale + phase.x, dy = padding * scale + phase.y,
                 nx = std::floor(dx), ny = std::floor(dy);
    const auto core = rasterize_at_scale(text, size, scale, bold, false, color, {dx - nx, dy - ny});
    // One extra column on the right: auto-sized glyph ink and outline are not cut by the bitmap edge.
    TextBitmap result{ui_image(core.image.width / scale + padding * 2 + 2 + phase.x / scale,
                               core.image.height / scale + padding * 2 + phase.y / scale, scale),
                      {-padding, -padding},
                      core.width,
                      core.height};
    ui_blit(result.image, core.image, nx / scale, ny / scale, false, true);
    finish(result.image, true, blur / 2);
    return result;
  };
  auto result = render({});
  result.redraw = std::make_shared<const std::function<TextBitmap(Vec2)>>(std::move(render));
  return result;
}

double FontBook::line_measure(std::string_view text, int size, bool bold) const {
  auto font = native_->faces[bold ? 1 : 0];
  double width{};
  FT_UInt previous{};
  for (std::size_t at = 0; at < text.size();) {
    const auto code = character(text, at);
    auto index = FT_Get_Char_Index(font, code);
    if (!index) {
      index = FT_Get_Char_Index(font, '?');
    }
    FT_Vector kern{};
    FT_Get_Kerning(font, previous, index, FT_KERNING_UNSCALED, &kern);
    if (FT_Load_Glyph(font, index, FT_LOAD_NO_SCALE)) {
      throw std::runtime_error("Cannot measure native glyph");
    }
    width +=
        static_cast<double>(font->glyph->metrics.horiAdvance + kern.x) * size / font->units_per_EM;
    previous = index;
  }
  return width;
}

double FontBook::line_ascent(int size, bool bold) const {
  const auto face = native_->faces[bold ? 1 : 0];
  return static_cast<double>(face->ascender) * size / face->units_per_EM;
}

TextBitmap FontBook::rasterize_line(std::string_view text, double scale, std::uint32_t color,
                                    int size, bool bold) const {
  if (!std::isfinite(scale) || scale <= 0 || scale > 16) {
    throw std::invalid_argument("Invalid chat text density");
  }
  auto bitmap = rasterize_native(text, size, bold, false, color, 0, 0, 1, 0, 0, scale, {}, true);
  bitmap.offset.y = -line_ascent(size, bold);
  return bitmap;
}

TextBitmap FontBook::rasterize_native(std::string_view text, int size, bool bold, bool shadow,
                                      std::uint32_t color, double phase, double vertical_phase,
                                      unsigned, int field_width, int field_height,
                                      double density, Vec2 pixel_phase,
                                      bool natural_metrics) const {
  REALM_ZONE("text.rasterize");
  auto run = layout(text, size, bold, false, phase, vertical_phase);
  if (natural_metrics) {
    run.width = line_measure(text, size, bold) + 4;
    run.height = size + 4;
  }
  if (field_width < 0 || field_height < 0) {
    throw std::invalid_argument("Invalid text field size");
  }

  const double padding = shadow ? 8 : 0;

  const double output_scale = density;
  const double w = field_width ? field_width : run.width;
  const double h = field_height ? field_height : run.height;
  TextBitmap result{
      ui_image(std::max(1.0, w + padding * 2 + pixel_phase.x / output_scale),
               std::max(1.0, h + padding * 2 + pixel_phase.y / output_scale), output_scale),
      {-padding - phase, -padding},
      run.width,
      run.height};
  auto face = native_->faces[bold ? 1 : 0];
  // Place the outlines after the TextField's 2 px gutter, with the first baseline at the
  // gutter plus the embedded font's ascent (usWinAscent; the baked line heights are win ascent +
  // descent). Keep the logical (1x TextField) advances so larger rendering does not change wrapping
  // or hit areas.
  auto& logical = this->face(size, bold, vertical_phase);
  const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
  if (!natural_metrics && !os2) {
    throw std::runtime_error("Cannot read native font metrics");
  }
  const double gutter = natural_metrics ? 0 : 2;
  const double baseline =
      natural_metrics ? line_ascent(size, bold)
                      : gutter + static_cast<double>(os2->usWinAscent) * size / face->units_per_EM;

  if (FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(std::lround(size * output_scale * 64)), 72,
                       72)) {
    throw std::runtime_error("Cannot size native font");
  }
  double x{}, y{};
  std::uint32_t previous{};

  for (std::size_t at = 0; at < text.size();) {
    auto code = character(text, at);
    if (code == '\r') {
      continue;
    }
    if (code == '\n') {
      x = 0;
      y += logical.line_advance;
      previous = 0;
      continue;
    }
    const auto* glyph = advance(logical, code);
    if (!glyph) {
      continue;
    }
    double natural_advance{};
    if (natural_metrics) {
      const auto index = FT_Get_Char_Index(face, code);
      FT_Vector kern{};
      FT_Get_Kerning(face, FT_Get_Char_Index(face, previous), index, FT_KERNING_UNSCALED, &kern);
      if (FT_Load_Glyph(face, index, FT_LOAD_NO_SCALE)) {
        throw std::runtime_error("Cannot measure chat glyph");
      }
      x += static_cast<double>(kern.x) * size / face->units_per_EM;
      natural_advance =
          static_cast<double>(face->glyph->metrics.horiAdvance) * size / face->units_per_EM;
    }
    const double origin = (x + phase + padding + gutter) * output_scale + pixel_phase.x;
    const double baseline_y =
        (y + baseline + vertical_phase + padding) * output_scale + pixel_phase.y;
    // Baselines snap to whole pixels so the light hinter's vertical grid fit stays sharp; pens
    // keep their sub-pixel horizontal phase.
    FT_Vector delta{static_cast<FT_Pos>(std::lround((origin - std::floor(origin)) * 64)), 0};
    FT_Set_Transform(face, nullptr, &delta);
    if (FT_Load_Char(face, code, glyph_load_flags)) {
      throw std::runtime_error("Cannot rasterize native glyph");
    }
    const auto& bitmap = face->glyph->bitmap;
    const int ox = static_cast<int>(std::floor(origin)) + face->glyph->bitmap_left;
    const int oy = static_cast<int>(std::lround(baseline_y)) - face->glyph->bitmap_top;
    for (unsigned row = 0; row < bitmap.rows; ++row) {
      for (unsigned col = 0; col < bitmap.width; ++col) {
        const int px = ox + static_cast<int>(col), py = oy + static_cast<int>(row);
        // Fixed fields keep a 2 px gutter on the right and bottom; auto-sized text is clipped by
        // its bitmap only.
        if (px < padding * output_scale + pixel_phase.x ||
            py < padding * output_scale + pixel_phase.y ||
            (field_width > 0 && px >= (padding + w - 2 + phase) * output_scale + pixel_phase.x) ||
            (field_height > 0 &&
             py >= (padding + h - 2 + vertical_phase) * output_scale + pixel_phase.y) ||
            px < 0 || py < 0 || px >= result.image.width || py >= result.image.height) {
          continue;
        }
        const auto dst =
            (static_cast<std::size_t>(py) * static_cast<std::size_t>(result.image.width) +
             static_cast<std::size_t>(px)) *
            4;
        const auto a = bitmap.buffer[static_cast<std::ptrdiff_t>(row) * bitmap.pitch + col];
        const unsigned combined =
            std::min(255U, static_cast<unsigned>(result.image.pixels[dst + 3]) + a);
        result.image.pixels[dst + 3] = static_cast<unsigned char>(combined);
        for (unsigned c = 0; c < 3; ++c) {
          result.image.pixels[dst + c] =
              static_cast<unsigned char>(((color >> (16 - c * 8)) & 255U) * (combined + 1U) >> 8U);
        }
      }
    }
    x += natural_metrics ? natural_advance : glyph->first;
    previous = code;
  }

  FT_Set_Transform(face, nullptr, nullptr);
  finish(result.image, shadow);
  return result;
}

FontFace& FontBook::face(int size, bool bold, double vertical_phase) const {
  if (size < 1 || size > 256) {
    throw std::runtime_error("Unsupported font size");
  }
  const int key = style(size, bold, false, vertical_phase);
  if (const auto found = faces_.find(key); found != faces_.end()) {
    return found->second;
  }
  const auto font = native_->faces[bold ? 1 : 0];
  const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(font, FT_SFNT_OS2));
  FontFace result;
  result.size = size;
  result.bold = bold;
  result.vertical_phase = vertical_phase;
  const long twips = static_cast<long>(size) *
                     (static_cast<long>(os2->usWinAscent) + os2->usWinDescent) * 20 /
                     font->units_per_EM;
  result.line_advance = static_cast<double>(twips) / 20;
  result.line_height = std::floor(result.line_advance) + 4;
  result.baseline =
      2 + static_cast<double>(os2->usWinAscent) * size / font->units_per_EM + vertical_phase;
  return faces_.emplace(key, std::move(result)).first->second;
}

const std::pair<double, bool>* FontBook::advance(FontFace& face, std::uint32_t& code) const {
  for (int attempt = 0; attempt < 2; ++attempt, code = '?') {
    if (const auto found = face.advances.find(code); found != face.advances.end()) {
      return &found->second;
    }
    const auto font = native_->faces[face.bold ? 1 : 0];
    const auto index = FT_Get_Char_Index(font, code);
    if (!index) {
      continue;
    }
    if (FT_Load_Glyph(font, index, FT_LOAD_NO_SCALE)) {
      throw std::runtime_error("Cannot measure native glyph");
    }
    // TextField advances are floored to twips.
    const long twips =
        static_cast<long>(font->glyph->metrics.horiAdvance) * face.size * 20 / font->units_per_EM;
    const bool empty =
        font->glyph->format == FT_GLYPH_FORMAT_OUTLINE && font->glyph->outline.n_contours == 0;
    return &face.advances.emplace(code, std::pair{static_cast<double>(twips) / 20, empty})
                .first->second;
  }
  return nullptr;
}

Image FontBook::render_glyph(const GlyphSource& source, Vec2* offset) const {
  const auto font = native_->faces[source.bold ? 1 : 0];
  const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(font, FT_SFNT_OS2));
  // The pen starts after the TextField's 2 px gutter; the first baseline sits at the gutter plus
  // usWinAscent.
  const double origin = 2 + source.x_phase,
               baseline = 2 +
                          static_cast<double>(os2->usWinAscent) * source.size / font->units_per_EM +
                          source.y_phase;
  if (FT_Set_Char_Size(font, 0, static_cast<FT_F26Dot6>(source.size) * 64, 72, 72)) {
    throw std::runtime_error("Cannot size native font");
  }
  FT_Vector delta{static_cast<FT_Pos>(std::lround((origin - std::floor(origin)) * 64)), 0};
  FT_Set_Transform(font, nullptr, &delta);
  const auto error = FT_Load_Char(font, source.code, glyph_load_flags);
  FT_Set_Transform(font, nullptr, nullptr);
  if (error) {
    throw std::runtime_error("Cannot rasterize native glyph");
  }
  const auto& bitmap = font->glyph->bitmap;
  if (offset) {
    *offset = {std::floor(origin) + font->glyph->bitmap_left,
               std::round(baseline) - font->glyph->bitmap_top};
  }
  Image result{static_cast<int>(std::max(1U, bitmap.width)),
               static_cast<int>(std::max(1U, bitmap.rows)),
               {}};
  result.pixels.resize(static_cast<std::size_t>(result.width) *
                       static_cast<std::size_t>(result.height) * 4);
  for (unsigned row = 0; row < bitmap.rows; ++row) {
    for (unsigned col = 0; col < bitmap.width; ++col) {
      const auto a = bitmap.buffer[static_cast<std::ptrdiff_t>(row) * bitmap.pitch + col];
      for (unsigned c = 0; c < 4; ++c) {
        result
            .pixels[(static_cast<std::size_t>(row) * static_cast<std::size_t>(result.width) + col) *
                        4 +
                    c] = a;
      }
    }
  }
  return result;
}

const Glyph& FontBook::placed(FontFace& face, std::uint32_t code, unsigned slot) const {
  const auto key = code * 32U + slot;
  if (const auto found = face.glyphs.find(key); found != face.glyphs.end()) {
    return found->second;
  }
  auto copy = code;
  const auto* metrics = advance(face, copy);
  Glyph glyph{sources_.size(), {}, metrics ? metrics->first : 0, !metrics || metrics->second};
  sources_.push_back({face.size, face.bold, code, slot / 20.0, face.vertical_phase});
  if (!glyph.empty) {
    auto rendered = render_glyph(sources_.back(), &glyph.offset);
    constexpr std::size_t limit = 8 * 1024 * 1024;
    while (!cache_.empty() &&
           (cache_bytes_ + rendered.pixels.size() > limit || cache_.size() >= 4096)) {
      const auto oldest = cache_.find(lru_.front());
      cache_bytes_ -= oldest->second.image.pixels.size();
      cache_.erase(oldest);
      lru_.pop_front();
      ++cache_evictions_;
      telemetry::add(telemetry::Counter::glyph_evictions);
    }
    cache_bytes_ += rendered.pixels.size();
    lru_.push_back(glyph.image);
    cache_.emplace(glyph.image, CachedGlyph{std::move(rendered), std::prev(lru_.end())});
  }
  return face.glyphs.emplace(key, glyph).first->second;
}

const Image& FontBook::image(std::size_t index) const {
  if (auto found = cache_.find(index); found != cache_.end()) {
    telemetry::add(telemetry::Counter::glyph_hits);
    lru_.splice(lru_.end(), lru_, found->second.position);
    return found->second.image;
  }
  telemetry::add(telemetry::Counter::glyph_misses);
  auto decoded = render_glyph(sources_.at(index), nullptr);
  constexpr std::size_t limit = 8 * 1024 * 1024;
  while (!cache_.empty() &&
         (cache_bytes_ + decoded.pixels.size() > limit || cache_.size() >= 4096)) {
    const auto oldest = cache_.find(lru_.front());
    cache_bytes_ -= oldest->second.image.pixels.size();
    cache_.erase(oldest);
    lru_.pop_front();
    ++cache_evictions_;
    telemetry::add(telemetry::Counter::glyph_evictions);
  }
  cache_bytes_ += decoded.pixels.size();
  lru_.push_back(index);
  return cache_.emplace(index, CachedGlyph{std::move(decoded), std::prev(lru_.end())})
      .first->second.image;
}

TextRun FontBook::layout(std::string_view text, int size, bool bold, bool, double phase,
                         double vertical_phase) const {
  if (!std::isfinite(phase) || phase < 0 || phase >= 1) {
    throw std::runtime_error("Invalid text horizontal phase");
  }
  auto& face = this->face(size, bold, vertical_phase);
  // Lines after the first sit 4 twips below their line advance, floored to the quarter-pixel glyph
  // grid.
  const auto advance_twips = std::lround(face.line_advance * 20);
  const auto base_twips = std::lround(vertical_phase * 20);
  TextRun run;
  run.glyphs.reserve(text.size());
  double x{}, line_top{};
  FontFace* line_face = &face;
  std::size_t lines = 1;

  for (std::size_t at = 0; at < text.size();) {
    const auto source = at;
    auto code = character(text, at);
    if (code == '\r') {
      continue;
    }
    if (code == '\n') {
      run.width = std::max(run.width, x);
      x = 0;
      const auto twips = base_twips + (static_cast<long>(lines) * advance_twips + 4) / 5 * 5;
      ++lines;
      line_top = static_cast<double>(twips / 20);
      line_face = &this->face(size, bold, static_cast<double>(twips % 20) / 20);
      continue;
    }
    const auto* metrics = advance(face, code);
    if (!metrics) {
      continue;
    }
    const double snapped = std::round((x + phase) * 20) / 20;
    const auto slot =
        static_cast<unsigned>(std::llround((snapped - std::floor(snapped)) * 20)) % 20U;
    if (!metrics->second) {
      const auto& glyph = placed(*line_face, code, slot);
      run.glyphs.push_back({glyph.image,
                            {std::floor(snapped) + glyph.offset.x, line_top + glyph.offset.y},
                            source,
                            snapped,
                            code});
    }
    x += metrics->first;
  }
  run.width = std::floor(std::max(run.width, x) + 4);
  run.height = static_cast<double>(lines) * face.line_height;
  return run;
}

double FontBook::line_advance(int size, bool bold) const {
  return face(size, bold).line_advance;
}

double FontBook::baseline(int size, bool bold) const {
  return face(size, bold).baseline;
}

double FontBook::measure(std::string_view text, int size, bool bold) const {
  auto& face = this->face(size, bold);
  double x{};
  for (std::size_t at = 0; at < text.size();) {
    auto code = character(text, at);
    if (const auto* metrics = advance(face, code)) {
      x += metrics->first;
    }
  }
  return x;
}

TextBitmap FontBook::rasterize(std::string_view text, int size, bool bold, bool shadow,
                               std::uint32_t color, double phase, double vertical_phase,
                               unsigned shadow_strength, int field_width, int field_height) const {
  if (pixel_scale_ != 1) {
    const double scale = pixel_scale_;
    auto render = [this, text = std::string(text), size, bold, shadow, color, phase, vertical_phase,
                   shadow_strength, field_width, field_height, scale](Vec2 pixel_phase) {
      return rasterize_native(text, size, bold, shadow, color, phase, vertical_phase,
                              shadow_strength, field_width, field_height, scale, pixel_phase);
    };
    auto result = render({});
    result.redraw = std::make_shared<const std::function<TextBitmap(Vec2)>>(std::move(render));
    return result;
  }

  const auto run = layout(text, size, bold, false, phase, vertical_phase);
  const int padding = shadow ? 8 : 0;
  if (run.width > 8192 || run.height > 8192) {
    throw std::runtime_error("Text field exceeds size limit");
  }
  if (field_width < 0 || field_width > 8192 || field_height < 0 || field_height > 8192) {
    throw std::runtime_error("Invalid text field size");
  }
  const int width = field_width ? field_width : static_cast<int>(run.width),
            height = field_height ? field_height : static_cast<int>(run.height);
  TextBitmap result{{std::max(1, width + 2 * padding), std::max(1, height + 2 * padding), {}},
                    {-static_cast<double>(padding) - phase, -static_cast<double>(padding)},
                    run.width,
                    run.height};
  result.image.pixels.resize(static_cast<std::size_t>(result.image.width) *
                             static_cast<std::size_t>(result.image.height) * 4);

  // Auto-sized text (zero field size) is clipped by its bitmap only; fixed fields keep a 2 px
  // gutter.
  const double right = field_width ? width - 2 + phase : width + padding,
               bottom = field_height ? height - 2 + vertical_phase : height + padding;
  for (const auto& glyph : run.glyphs) {
    composite(result.image, image(glyph.image), static_cast<int>(glyph.position.x),
              static_cast<int>(glyph.position.y), padding, right, bottom, color);
  }
  finish(result.image, shadow);
  return result;
}

TextBitmap FontBook::rasterize_spans(std::span<const TextSpan> spans, int size, bool shadow,
                                     std::uint32_t color, double bold_vertical_phase) const {
  if (pixel_scale_ != 1) {
    struct Owned {
      std::string text;
      bool bold{};
    };

    std::vector<Owned> owned;
    owned.reserve(spans.size());
    for (const auto& span : spans) {
      owned.push_back({std::string(span.text), span.bold});
    }

    const double scale = pixel_scale_;
    auto render = [this, owned = std::move(owned), size, shadow, color, bold_vertical_phase,
                   scale](Vec2 pixel_phase) {
      double advance{}, height{};
      for (const auto& span : owned) {
        advance += measure(span.text, size, span.bold);
        height = std::max(height, layout(span.text, size, span.bold).height);
      }
      const double padding = shadow ? 8 : 0;
      TextBitmap result{ui_image(std::floor(advance + 4) + 2 * padding + pixel_phase.x / scale,
                                 height + 2 * padding + pixel_phase.y / scale, scale),
                        {-padding, -padding},
                        std::floor(advance + 4),
                        height};
      advance = 0;
      for (const auto& span : owned) {
        if (span.text.empty()) {
          continue;
        }
        const double dx = (padding + advance) * scale + pixel_phase.x,
                     dy = padding * scale + pixel_phase.y, nx = std::floor(dx), ny = std::floor(dy);
        const auto part = rasterize_native(span.text, size, span.bold, false, color, 0,
                                           span.bold ? bold_vertical_phase : 0, 1, 0, 0, scale,
                                           {dx - nx, dy - ny});
        ui_blit(result.image, part.image, nx / scale, ny / scale);
        advance += measure(span.text, size, span.bold);
      }
      // Apply the filter once to the complete field, including mixed spans.
      premultiply_flash_bitmap(result.image);
      finish(result.image, shadow);
      return result;
    };
    auto result = render({});
    result.redraw = std::make_shared<const std::function<TextBitmap(Vec2)>>(std::move(render));
    return result;
  }

  TextRun combined;
  double advance{};
  for (const auto& span : spans) {
    if (span.text.empty()) {
      continue;
    }
    const auto run = layout(span.text, size, span.bold, false, advance - std::floor(advance),
                            span.bold ? bold_vertical_phase : 0);
    for (const auto& glyph : run.glyphs) {
      combined.glyphs.push_back(
          {glyph.image, {glyph.position.x + std::floor(advance), glyph.position.y}});
    }
    advance += measure(span.text, size, span.bold);
    combined.height = std::max(combined.height, run.height);
  }
  combined.width = std::floor(advance + 4);
  const int padding = shadow ? 8 : 0;
  if (combined.width > 8192 || combined.height > 8192) {
    throw std::runtime_error("Text field exceeds size limit");
  }
  const int w = static_cast<int>(combined.width), h = static_cast<int>(combined.height);
  TextBitmap result{{std::max(1, w + padding * 2), std::max(1, h + padding * 2), {}},
                    {-static_cast<double>(padding), -static_cast<double>(padding)},
                    combined.width,
                    combined.height};
  result.image.pixels.resize(static_cast<std::size_t>(result.image.width) *
                             static_cast<std::size_t>(result.image.height) * 4);

  // Auto-sized spans, like auto-sized text, are clipped by their bitmap only.
  for (const auto& glyph : combined.glyphs) {
    composite(result.image, image(glyph.image), static_cast<int>(glyph.position.x),
              static_cast<int>(glyph.position.y), padding, w + padding, h + padding, color);
  }
  finish(result.image, shadow);
  return result;
}

TextBitmap FontBook::rasterize_rich(std::span<const RichLine> lines, int size, bool field_bold,
                                    int field_width, double field_height, double vertical_phase,
                                    TextFilter filter) const {
  if (field_width < 4 || field_width > 8192 || !(field_height >= 0 && field_height <= 8192) ||
      !(vertical_phase >= 0 && vertical_phase < 1)) {
    throw std::runtime_error("Invalid rich text field");
  }

  const auto advance = std::lround(line_advance(size, field_bold) * 20);
  const auto base = std::lround(vertical_phase * 20);
  if (base % 5) {
    throw std::runtime_error("Rich text fields sit on a quarter-pixel grid");
  }
  // Lines after the first use a three-twip bias before flooring to the quarter-pixel glyph grid.
  const auto line_y = [advance, base](std::size_t i) {
    const long twips = base + static_cast<long>(i) * advance + (i ? 3 : 0);
    return static_cast<double>(twips / 5 * 5) / 20;
  };
  const double height =
      field_height > 0 ? field_height
                       : static_cast<double>(lines.size()) * static_cast<double>(advance) / 20 + 4;
  const double bottom = field_height > 0 ? field_height + vertical_phase : 1e9;
  const bool outlined = filter == TextFilter::outline;
  auto text_of = [](const RichLine& line) {
    std::string text;
    for (const auto& span : line.spans) {
      text += span.text;
    }
    return text;
  };
  if (pixel_scale_ != 1) {
    const double scale = pixel_scale_;
    std::vector<RichLine> owned(lines.begin(), lines.end());
    auto render = [this, owned = std::move(owned), size, field_bold, field_width, height, bottom,
                   outlined, scale, line_y, text_of](Vec2 pixel_phase) {
      const double padding = outlined ? 8 : 0;
      TextBitmap result{
          ui_image(field_width + 2 * padding + pixel_phase.x / scale,
                   std::ceil(height) + 1 + 2 * padding + pixel_phase.y / scale, scale),
          {-padding, -padding},
          static_cast<double>(field_width),
          height};
      for (std::size_t i = 0; i < owned.size(); ++i) {
        const auto& line = owned[i];
        const auto text = text_of(line);
        std::size_t start{};
        for (const auto& span : line.spans) {
          if (!span.text.empty()) {
            const double dx = (padding + line.x +
                               measure(std::string_view(text).substr(0, start), size, line.bold)) *
                                  scale +
                              pixel_phase.x;
            const double dy = (padding + line_y(i) + (line.bold && !field_bold ? .75 : 0)) * scale +
                              pixel_phase.y,
                         nx = std::floor(dx), ny = std::floor(dy);
            const auto part = rasterize_native(span.text, size, line.bold, false, span.color, 0, 0,
                                               1, 0, 0, scale, {dx - nx, dy - ny});
            ui_blit(result.image, part.image, nx / scale, ny / scale, false, false, 1,
                    padding + pixel_phase.x / scale, padding + pixel_phase.y / scale,
                    field_width - 2, bottom);
          }
          start += span.text.size();
        }
      }
      premultiply_flash_bitmap(result.image);
      if (outlined) {
        ui_outline_alpha(result.image);
      }
      finish(result.image, false);
      return result;
    };
    auto result = render({});
    result.redraw = std::make_shared<const std::function<TextBitmap(Vec2)>>(std::move(render));
    return result;
  }

  const int padding = outlined ? 8 : 0;
  if (height > 8192) {
    throw std::runtime_error("Text field exceeds size limit");
  }
  TextBitmap result{{field_width + 2 * padding,
                     static_cast<int>(std::ceil(height + vertical_phase)) + 2 * padding,
                     {}},
                    {-static_cast<double>(padding), -static_cast<double>(padding)},
                    static_cast<double>(field_width),
                    height};
  result.image.pixels.resize(static_cast<std::size_t>(result.image.width) *
                             static_cast<std::size_t>(result.image.height) * 4);

  for (std::size_t i = 0; i < lines.size(); ++i) {
    const auto& line = lines[i];
    const auto text = text_of(line);
    std::vector<std::pair<std::size_t, std::uint32_t>> colors;
    std::size_t start{};
    for (const auto& span : line.spans) {
      colors.emplace_back(start, span.color);
      start += span.text.size();
    }
    const double x = line.x, y = line_y(i) + (line.bold && !field_bold ? .75 : 0);
    const auto run = layout(text, size, line.bold, false, x - std::floor(x), y - std::floor(y));
    for (const auto& glyph : run.glyphs) {
      std::uint32_t color = colors.empty() ? 0 : colors.front().second;
      for (const auto& [from, value] : colors) {
        if (from <= glyph.source) {
          color = value;
        }
      }
      composite(result.image, image(glyph.image),
                static_cast<int>(std::floor(x) + glyph.position.x),
                static_cast<int>(std::floor(y) + glyph.position.y), padding, field_width - 2,
                bottom, color);
    }
  }
  if (outlined) {
    ui_outline_alpha(result.image);
  }
  finish(result.image, false);
  return result;
}

std::vector<std::string> FontBook::wrap(std::string_view text, int size, bool bold,
                                        int field_width) const {
  const double limit = field_width - 4;
  std::vector<std::string> lines;
  std::string current;
  for (std::size_t at = 0; at <= text.size();) {
    const auto newline = text.find('\n', at);
    const auto paragraph =
        text.substr(at, newline == std::string_view::npos ? std::string_view::npos : newline - at);
    for (std::size_t begin = 0; begin < paragraph.size();) {
      // A token is a word with the spaces after it; those spaces do not
      // count toward the line width.
      auto end = paragraph.find_first_not_of(' ', begin);
      end = end == std::string_view::npos ? paragraph.size() : paragraph.find(' ', end);
      end =
          end == std::string_view::npos ? paragraph.size() : paragraph.find_first_not_of(' ', end);
      if (end == std::string_view::npos) {
        end = paragraph.size();
      }
      const auto hyphen = paragraph.find('-', begin);
      if (hyphen != std::string_view::npos && hyphen + 1 < end) {
        end = hyphen + 1;
      }
      const auto token = paragraph.substr(begin, end - begin);
      begin = end;
      const auto word = trailing_trimmed(token);
      if (!current.empty() &&
          measure(trailing_trimmed(current + std::string(word)), size, bold) > limit) {
        lines.push_back(std::move(current));
        current.clear();
      }
      if (current.empty() && measure(word, size, bold) > limit) {
        // A word wider than the field breaks between characters.
        for (std::size_t c = 0; c < token.size();) {
          const auto from = c;
          (void)character(token, c);
          const auto piece = token.substr(from, c - from);
          if (!current.empty() && piece != " " &&
              measure(trailing_trimmed(current + std::string(piece)), size, bold) > limit) {
            lines.push_back(std::move(current));
            current.clear();
          }
          current += piece;
        }
        continue;
      }
      current += token;
    }
    lines.push_back(std::move(current));
    current.clear();
    if (newline == std::string_view::npos) {
      break;
    }
    at = newline + 1;
  }
  return lines;
}

TextBitmap FontBook::rasterize_centered(std::string_view text, int size, bool bold, bool shadow,
                                        std::uint32_t color, int field_width,
                                        unsigned shadow_strength) const {
  if (field_width < 4 || field_width > 8192) {
    throw std::runtime_error("Invalid text field width");
  }

  const auto lines = wrap(text, size, bold, field_width);
  // Line positions are twips: x = 2 + (width - 4 - line width) / 2, floored.
  const auto advance = std::lround(line_advance(size, bold) * 20);
  const double height = static_cast<double>(lines.size()) * static_cast<double>(advance) / 20 + 4;
  if (pixel_scale_ != 1) {
    const double scale = pixel_scale_;
    auto render = [this, lines, size, bold, shadow, color, field_width, shadow_strength, advance,
                   height, scale](Vec2 pixel_phase) {
      const double padding = shadow ? 8 : 0;
      TextBitmap result{ui_image(field_width + 2 * padding + pixel_phase.x / scale,
                                 std::ceil(height) + 2 * padding + pixel_phase.y / scale, scale),
                        {-padding, -padding},
                        static_cast<double>(field_width),
                        height};
      for (std::size_t i = 0; i < lines.size(); ++i) {
        const double dx =
            (padding + (field_width - 4 - measure(trailing_trimmed(lines[i]), size, bold)) / 2) *
                scale +
            pixel_phase.x;
        const double dy = (padding + static_cast<double>(i) * static_cast<double>(advance) / 20) *
                              scale +
                          pixel_phase.y,
                     nx = std::floor(dx), ny = std::floor(dy);
        const auto part = rasterize_native(lines[i], size, bold, false, color, 0, 0, 1, 0, 0, scale,
                                           {dx - nx, dy - ny});
        ui_blit(result.image, part.image, nx / scale, ny / scale);
      }
      premultiply_flash_bitmap(result.image);
      finish(result.image, shadow);
      return result;
    };
    auto result = render({});
    result.redraw = std::make_shared<const std::function<TextBitmap(Vec2)>>(std::move(render));
    return result;
  }

  if (height > 8192) {
    throw std::runtime_error("Text field exceeds size limit");
  }
  const int padding = shadow ? 8 : 0;
  TextBitmap result{
      {field_width + 2 * padding, static_cast<int>(std::ceil(height)) + 2 * padding, {}},
      {-static_cast<double>(padding), -static_cast<double>(padding)},
      static_cast<double>(field_width),
      height};
  result.image.pixels.resize(static_cast<std::size_t>(result.image.width) *
                             static_cast<std::size_t>(result.image.height) * 4);

  for (std::size_t i = 0; i < lines.size(); ++i) {
    const auto width = std::lround(measure(trailing_trimmed(lines[i]), size, bold) * 20);
    const auto twips =
        static_cast<long>(std::floor(static_cast<double>((field_width - 4) * 20 - width) / 2));
    // Lines after the first sit 4 twips below their TextLineMetrics position,
    // floored to the quarter-pixel vertical glyph grid (measured on
    // second to fifth 16- and 18-pixel bold lines).
    const auto line_twips = static_cast<long>(i) * advance + (i ? 4 : 0);
    const double x = static_cast<double>(twips) / 20,
                 y = static_cast<double>(line_twips / 5 * 5) / 20;
    const auto run = layout(lines[i], size, bold, false, x - std::floor(x), y - std::floor(y));
    for (const auto& glyph : run.glyphs) {
      composite(result.image, image(glyph.image),
                static_cast<int>(std::floor(x) + glyph.position.x),
                static_cast<int>(std::floor(y) + glyph.position.y), padding, field_width - 2,
                height - 2, color);
    }
  }
  finish(result.image, shadow);
  return result;
}

TextBitmap redrawable_text(std::function<TextBitmap(Vec2)> render) {
  auto result = render({});
  result.redraw = std::make_shared<const std::function<TextBitmap(Vec2)>>(std::move(render));
  return result;
}

TextBitmap place_text(const TextBitmap& text, double scale, double& x, double& y) {
  if (!text.redraw || scale == 1 || text.image.pixel_scale != scale || !std::isfinite(x) ||
      !std::isfinite(y)) {
    return text;
  }
  const double dx = x * scale, dy = y * scale, nx = std::floor(dx), ny = std::floor(dy);
  auto exact = (*text.redraw)({dx - nx, dy - ny});
  x = nx / scale;
  y = ny / scale;
  return exact;
}

void ui_blit_text(Image& destination, const TextBitmap& text, double x, double y,
                  bool source_premultiplied, bool destination_premultiplied, double opacity,
                  double clip_x, double clip_y, double clip_width, double clip_height) {
  const auto placed = place_text(text, destination.pixel_scale, x, y);
  ui_blit(destination, placed.image, x, y, source_premultiplied, destination_premultiplied, opacity,
          clip_x, clip_y, clip_width, clip_height);
}
}
