#pragma once
#include "realm/font.hpp"
#include "realm/hud.hpp"
#include "realm/hud_art.hpp"
#include "realm/image.hpp"
#include "realm/ui_stack.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Rml {
class Context;
class Element;
class ElementDocument;
class Event;
}

// The client's one RmlUi context: the graphics half of the shared UI foundation.
//
// RmlUi provides layout, styling, focus, scrolling, clipping and text editing. Three client rules hold for every
// document:
// - text is laid out and rasterized only by FontBook (logical metrics, drawable-resolution outlines, quarter-pixel
//   phase, Flash's baked glyphs at exactly 1x);
// - Flash pixel art (HudArt `art:KEY`, items `item:TYPE`) is sampled nearest;
// - Flash vector shapes (cut-edge rects, menu backdrops, button frames) are drawn by the client's own painters,
//   rasterized at drawable density into retained textures, so RmlUi screens match the native ones they replace.
// RmlUi is a global library: exactly one RmlHost may exist, created and destroyed with the window's GL context current.
namespace realm::ui {

struct RmlStats {
  // Cumulative since construction or the last reset_stats().
  std::uint64_t text_rasterizations{}, painter_rasterizations{}, texture_uploads{}, texture_upload_bytes{};
  std::uint64_t text_meshes{};  // GenerateString calls (text geometry regenerations)
  std::uint64_t native_image_uploads{}, native_image_upload_bytes{}, native_image_meshes{};
  std::uint64_t document_loads{}, document_load_us{}, document_load_bytes{};
  std::uint64_t log_warnings{}, log_errors{};
  // Last frame.
  std::size_t draws{};  // geometry draws in the last render()
  double gpu_ms{-1};    // GPU time of the most recent completed render() pass
  // Current residency.
  std::size_t text_cache_bytes{}, text_cache_entries{}, painter_cache_bytes{}, painter_cache_entries{},
      native_image_bytes{}, documents{};
};

// Arguments a painter receives: the element's border box in logical px, the drawable density and the decorator's
// own parameters (RCSS `decorator: paint(NAME ARG...)`; ARGs are space-separated tokens, e.g. colors as 0xRRGGBB).
struct PaintRequest {
  std::string name;
  std::vector<std::string> args;
  double width{}, height{};  // logical px
  double scale{1};           // drawable px per logical px
};
// Returns a straight-alpha image of ceil(width*scale) x ceil(height*scale) px whose pixel_scale is `scale`.
using Painter = std::function<Image(const PaintRequest&)>;

class RmlHost {
 public:
  RmlHost(const FontBook& fonts, const HudArt* art);
  ~RmlHost();
  RmlHost(const RmlHost&) = delete;
  RmlHost& operator=(const RmlHost&) = delete;

  [[nodiscard]] Rml::Context& context();
  // Documents embedded from ui/NAME.rml at build time. Every document may <link> ui/flash.rcss; the host also injects
  // the FontBook text classes (.t14, .t14b, .t16 ... with dp line heights) into each loaded document.
  [[nodiscard]] Rml::ElementDocument* load(std::string_view name);
  [[nodiscard]] Rml::ElementDocument* load_from_memory(const std::string& rml, std::string_view source_name);
  // Closes the document (deferred to the next update() by RmlUi) and clears the pointer.
  void close(Rml::ElementDocument*& document);
  // Shows a document. A modal document blocks the documents below it and makes the host's layer modal.
  void show(Rml::ElementDocument& document, bool modal = false, bool focus = true);
  void hide(Rml::ElementDocument& document);

  void register_painter(std::string name, Painter painter);
  // A listener whose lifetime is tied to `owner`: removed with it. Returns nothing to keep; the host owns it.
  void listen(Rml::Element& element, const char* event, std::function<void(Rml::Event&)> function,
              bool capture = false);

  void resize(const Viewport& viewport);  // context dimensions, dp ratio = viewport.scale(), FontBook density
  void update(std::int32_t time_ms);      // deterministic clock for animations; trims caches; Context::Update
  void render(int drawable_width, int drawable_height);  // RmlUi's GL pass, after Renderer::draw
  // Releases every cached text/painter texture (e.g. map change); they are re-created on demand.
  void release_caches();

  // The whole context as one input layer: active while any document is visible; modal while a modal document is
  // visible; covers a point when an element other than a document body is under it; wants text while a text
  // control has focus.
  [[nodiscard]] Layer& layer();
  [[nodiscard]] RmlStats stats() const;
  void reset_stats();
  // RCSS text classes generated from FontBook metrics, e.g. ".t16b{font-size:16dp;line-height:Ndp;font-weight:bold}".
  [[nodiscard]] std::string text_classes() const;
  [[nodiscard]] const FontBook& fonts() const;
  [[nodiscard]] const HudArt* art() const;
  [[nodiscard]] double scale() const;
  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

// Native-owned images are uploaded synchronously and retained by <native-image>. Reusing the same Image object and
// revision shares its texture between elements; callers increment revision whenever the pixels change.
void set_native_image(Rml::Element& element, const Image& image, std::uint64_t revision,
                      std::span<const HudRegion> dirty = {});
void set_native_source(Rml::Element& element, HudRegion source);
void set_native_tint(Rml::Element& element, std::array<float, 4> multiply);

// The embedded ui/ files (name without directory -> contents), generated at build time.
[[nodiscard]] const std::string* embedded_ui_file(std::string_view name);
}
