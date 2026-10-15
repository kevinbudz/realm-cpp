#pragma once
#include "realm/assets/Image.hpp"
#include "realm/map/Camera.hpp"
#include <string_view>
#include <unordered_map>
#include <list>
#include <memory>
#include <functional>

namespace realm {
struct Glyph {
  std::size_t image{};
  Vec2 offset{};
  double advance{};
  bool empty{};
};

// One size/weight/vertical phase of the original Myriad outlines. Metrics follow TextField:
// advances are floored to twips and the line advance is the OS/2 win ascent plus descent, floored
// to twips. Glyphs (keyed by code*32 + twentieth-pixel phase) are rasterized on first use.
struct FontFace {
  int size{};
  bool bold{};
  double vertical_phase{};
  double line_height{};
  double line_advance{};
  double baseline{};
  std::unordered_map<std::uint32_t, Glyph> glyphs;
  std::unordered_map<std::uint32_t, std::pair<double, bool>> advances;
};

// `source` is the byte offset of the glyph's character in the laid-out text.
struct GlyphPlacement {
  std::size_t image{};
  Vec2 position{};
  std::size_t source{};
  double origin_x{};
  std::uint32_t code{};
};

struct TextRun {
  double width{};
  double height{};
  std::vector<GlyphPlacement> glyphs;
};

// Bitmaps rasterized at a non-unit density carry a recipe that re-renders them with the glyphs
// shifted by a sub-pixel phase in [0,1), so place_text can put the text at its exact device
// position instead of rounding the whole bitmap origin to a pixel.
struct TextBitmap {
  Image image;
  Vec2 offset{};
  double width{};
  double height{};
  std::shared_ptr<const std::function<TextBitmap(Vec2)>> redraw{};
};

// Re-renders text for blitting at logical (x,y) on a surface of density scale (x,y already include
// the bitmap offset). Replaces x,y with the whole-pixel origin to blit the result at. Returns the
// input if it cannot be refined.
[[nodiscard]] TextBitmap place_text(const TextBitmap& text, double scale, double& x, double& y);
// Builds a bitmap from render(phase) and keeps render as its recipe, for text composed from other
// text bitmaps (compose them with place_text at the phase offset so every part lands at its exact
// device position).
[[nodiscard]] TextBitmap redrawable_text(std::function<TextBitmap(Vec2)> render);
void ui_blit_text(Image& destination, const TextBitmap& text, double x, double y,
                  bool source_premultiplied = false, bool destination_premultiplied = false,
                  double opacity = 1, double clip_x = 0, double clip_y = 0, double clip_width = 1e9,
                  double clip_height = 1e9);

struct TextSpan {
  std::string_view text;
  bool bold{};
};

// One line of an htmlText TextField: colored spans in one weight, starting `x` pixels into the
// field.
struct RichSpan {
  std::string text;
  std::uint32_t color{};
};

struct RichLine {
  std::vector<RichSpan> spans;
  bool bold{};
  double x{};
};
// EquipmentToolTip's DropShadowFilter(0,0,0,.5,12,12) on text fields.
enum class TextFilter { none, soft_shadow };

// Text from the original regular/bold Myriad OTFs in the Flash source, rasterized with FreeType.
class FontBook {
 public:
  // Loads the fonts from the configured Flash source root.
  FontBook();
  explicit FontBook(const std::filesystem::path& flash_root);
  ~FontBook();
  void set_pixel_scale(double scale);

  [[nodiscard]] double pixel_scale() const { return pixel_scale_; }

  // Animated text is rasterized at its current physical size, including at 1x.
  [[nodiscard]] TextBitmap rasterize_at_scale(std::string_view text, int size, double scale,
                                              bool bold = false, bool shadow = false,
                                              std::uint32_t color = 0xffffff,
                                              Vec2 pixel_phase = {}) const;
  // GlowFilter blur (blurX = blurY) and strength. blur 4 is the 5-tap filter at 1x
  // (CharacterStatusText); blur 2 is the legacy radius-1 filter (ItemTile tier tags). The default
  // stays at 2 for existing glow callers.
  [[nodiscard]] TextBitmap rasterize_glow(std::string_view text, int size, bool bold,
                                          std::uint32_t color, unsigned strength = 10,
                                          double blur = 2) const;
  // Chat's CFF TextLine uses natural font advances and a baseline origin,
  // rather than SimpleText's TextField metrics and gutters.
  [[nodiscard]] double line_measure(std::string_view text, int size = 14, bool bold = true) const;
  [[nodiscard]] double line_ascent(int size = 14, bool bold = true) const;
  [[nodiscard]] TextBitmap rasterize_line(std::string_view text, double scale,
                                          std::uint32_t color = 0xffffff, int size = 14,
                                          bool bold = true) const;
  // phase and vertical_phase lie on the twentieth-pixel grid. Bitmap offsets are relative to the
  // integer part of the origin. A zero field_width or field_height clips like an auto-sized
  // TextField; otherwise the field has that fixed size.
  [[nodiscard]] TextRun layout(std::string_view text, int size, bool bold = false,
                               bool shadow = false, double phase = 0,
                               double vertical_phase = 0) const;
  [[nodiscard]] double line_advance(int size, bool bold = false) const;
  // Distance from a native-rendered TextField's top edge to its first baseline: the 2 px gutter
  // plus the embedded font's usWinAscent.
  [[nodiscard]] double baseline(int size, bool bold = false) const;
  // Unrounded advance width of a single line, as TextLineMetrics.width.
  [[nodiscard]] double measure(std::string_view text, int size, bool bold = false) const;
  [[nodiscard]] TextBitmap rasterize(std::string_view text, int size, bool bold = false,
                                     bool shadow = false, std::uint32_t color = 0xffffff,
                                     double phase = 0, double vertical_phase = 0,
                                     unsigned shadow_strength = 1, int field_width = 0,
                                     int field_height = 0) const;
  // One auto-sized single-line TextField with mixed bold/regular spans.
  [[nodiscard]] TextBitmap rasterize_spans(std::span<const TextSpan> spans, int size, bool shadow,
                                           std::uint32_t color,
                                           double bold_vertical_phase = 0) const;
  // A wordWrap TextField of RichLines, `field_bold` choosing its default line advance. The field
  // origin's vertical phase is a multiple of a quarter pixel; later lines follow the same twip grid
  // as rasterize_centered. The image origin is the field origin's whole pixel; field_height clips
  // like the TextField (zero: no clip).
  [[nodiscard]] TextBitmap rasterize_rich(std::span<const RichLine> lines, int size,
                                          bool field_bold, int field_width, double field_height,
                                          double vertical_phase, TextFilter filter) const;
  // TextField wordWrap lines, each keeping the spaces that follow its last word.
  [[nodiscard]] std::vector<std::string> wrap(std::string_view text, int size, bool bold,
                                              int field_width) const;
  // A wordWrap TextField of fixed width with centered paragraphs. The image
  // origin is the field origin; height is the field height (textHeight + 4).
  [[nodiscard]] TextBitmap rasterize_centered(std::string_view text, int size, bool bold,
                                              bool shadow, std::uint32_t color, int field_width,
                                              unsigned shadow_strength = 1) const;

  [[nodiscard]] std::size_t cache_bytes() const { return cache_bytes_; }

  [[nodiscard]] std::size_t cache_entries() const { return cache_.size(); }

  [[nodiscard]] std::uint64_t cache_evictions() const { return cache_evictions_; }

 private:
  struct NativeFonts;
  std::unique_ptr<NativeFonts> native_;
  double pixel_scale_{1};
  [[nodiscard]] TextBitmap rasterize_native(std::string_view text, int size, bool bold, bool shadow,
                                            std::uint32_t color, double phase,
                                            double vertical_phase, unsigned strength,
                                            int field_width, int field_height, double density,
                                            Vec2 pixel_phase = {},
                                            bool natural_metrics = false) const;

  struct GlyphSource {
    int size{};
    bool bold{};
    std::uint32_t code{};
    double x_phase{}, y_phase{};
  };

  struct CachedGlyph {
    Image image;
    std::list<std::size_t>::iterator position;
  };

  [[nodiscard]] const Image& image(std::size_t index) const;
  [[nodiscard]] Image render_glyph(const GlyphSource& source, Vec2* offset) const;
  [[nodiscard]] FontFace& face(int size, bool bold, double vertical_phase = 0) const;
  // Advance in pixels and whether the glyph has no outline; falls back to '?' when the font lacks
  // the character.
  [[nodiscard]] const std::pair<double, bool>* advance(FontFace& face, std::uint32_t& code) const;
  [[nodiscard]] const Glyph& placed(FontFace& face, std::uint32_t code, unsigned slot) const;
  mutable std::unordered_map<int, FontFace> faces_;
  mutable std::vector<GlyphSource> sources_;
  mutable std::unordered_map<std::size_t, CachedGlyph> cache_;
  mutable std::list<std::size_t> lru_;
  mutable std::size_t cache_bytes_{};
  mutable std::uint64_t cache_evictions_{};
};
}
