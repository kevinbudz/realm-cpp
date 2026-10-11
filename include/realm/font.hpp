#pragma once
#include "realm/image.hpp"
#include "realm/camera.hpp"
#include <string_view>
#include <unordered_map>
#include <fstream>
#include <list>
namespace realm {
struct Glyph { std::size_t image{};Vec2 offset{};double advance{};bool empty{}; };
struct FontFace {
    double line_height{},line_advance{};
    int phases{1};
    bool loaded{true};
    std::uint64_t first{},count{};
    std::unordered_map<std::uint32_t,Glyph> glyphs;
    std::unordered_map<std::uint64_t,double> kerning;
};
struct GlyphPlacement { std::size_t image{};Vec2 position{}; };
struct TextRun { double width{},height{};std::vector<GlyphPlacement> glyphs; };
struct TextBitmap { Image image;Vec2 offset{};double width{},height{}; };
struct TextSpan { std::string_view text;bool bold{}; };
class FontBook {
public:
    explicit FontBook(const std::filesystem::path& directory);
    // vertical_phase is a quarter pixel; the corresponding source coverage must be
    // baked. Bitmap offsets are relative to the integer part of the origin.
    // A zero field_width or field_height clips like an auto-sized TextField;
    // otherwise the field has that fixed size.
    [[nodiscard]] TextRun layout(std::string_view text,int size,bool bold=false,bool shadow=false,double phase=0,double vertical_phase=0) const;
    // Unrounded advance width of a single line, as TextLineMetrics.width.
    [[nodiscard]] double measure(std::string_view text,int size,bool bold=false) const;
    [[nodiscard]] TextBitmap rasterize(std::string_view text,int size,bool bold=false,bool shadow=false,std::uint32_t color=0xffffff,double phase=0,double vertical_phase=0,unsigned shadow_strength=1,int field_width=0,int field_height=0) const;
    // One auto-sized single-line TextField with mixed bold/regular spans.
    [[nodiscard]] TextBitmap rasterize_spans(std::span<const TextSpan> spans,int size,bool shadow,std::uint32_t color,double bold_vertical_phase=0) const;
    // TextField wordWrap lines, each keeping the spaces that follow its last word.
    [[nodiscard]] std::vector<std::string> wrap(std::string_view text,int size,bool bold,int field_width) const;
    // A wordWrap TextField of fixed width with centered paragraphs. The image
    // origin is the field origin; height is the field height (textHeight + 4).
    [[nodiscard]] TextBitmap rasterize_centered(std::string_view text,int size,bool bold,bool shadow,std::uint32_t color,int field_width,unsigned shadow_strength=1) const;
    [[nodiscard]] bool empty() const { return faces_.empty(); }
    [[nodiscard]] std::size_t cache_bytes() const { return cache_bytes_; }
    [[nodiscard]] std::size_t cache_entries() const { return cache_.size(); }
    [[nodiscard]] std::uint64_t cache_evictions() const { return cache_evictions_; }
private:
    struct GlyphSource { std::uint64_t offset{};std::uint32_t length{};int width{},height{};std::uint32_t path{}; };
    struct CachedGlyph { Image image;std::list<std::size_t>::iterator position; };
    [[nodiscard]] const Image& image(std::size_t index) const;
    void load_face(FontFace& face) const;
    std::filesystem::path directory_;
    mutable std::unordered_map<int,FontFace> faces_;
    mutable std::vector<GlyphSource> sources_;
    std::vector<std::string> paths_;
    mutable std::ifstream bundle_,index_;
    std::uint64_t bundle_size_{};
    mutable std::unordered_map<std::size_t,CachedGlyph> cache_;
    mutable std::list<std::size_t> lru_;
    mutable std::size_t cache_bytes_{};
    mutable std::uint64_t cache_evictions_{};
};
}
