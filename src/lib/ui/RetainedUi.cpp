#include "realm/lib/ui/RetainedUi.hpp"
#include "realm/util/TextFormat.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

namespace realm::ui {
namespace {
std::atomic<std::uint64_t> revision_source{};

std::uint64_t next_revision() {
  return revision_source.fetch_add(1, std::memory_order_relaxed) + 1;
}

void validate_image(const Image& image) {
  if (image.width < 1 || image.height < 1 ||
      image.pixels.size() !=
          static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4) {
    throw std::invalid_argument("Invalid UI texture upload");
  }
}

HudRegion unite(HudRegion a, HudRegion b) {
  const int x = std::min(a.x, b.x);
  const int y = std::min(a.y, b.y);
  const int right = std::max(a.x + a.width, b.x + b.width);
  const int bottom = std::max(a.y + a.height, b.y + b.height);
  return {x, y, right - x, bottom - y};
}

void set_full_image(RetainedSurface::State& state, const Image& image);
void copy_image_region(RetainedSurface::State& state, const Image& image, int x, int y, int width,
                       int height);
void mark_dirty(RetainedSurface::State& state, HudRegion region);

std::string px(double value) {
  return trimmed_number(value) + "px";
}

std::string rgba(const std::array<float, 4>& color) {
  char buffer[16];
  const auto channel = [](float value) {
    return static_cast<unsigned>(std::lround(std::clamp(value, 0.F, 1.F) * 255));
  };
  std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x%02x", channel(color[0]), channel(color[1]),
                channel(color[2]), channel(color[3]));
  return buffer;
}

bool axis_aligned(const Quad& quad) {
  constexpr float epsilon = 1e-4F;
  return std::isfinite(quad.origin[0]) && std::isfinite(quad.origin[1]) &&
         std::isfinite(quad.axis_x[0]) && std::isfinite(quad.axis_x[1]) &&
         std::isfinite(quad.axis_y[0]) && std::isfinite(quad.axis_y[1]) &&
         std::abs(quad.axis_x[1]) <= epsilon && std::abs(quad.axis_y[0]) <= epsilon &&
         quad.axis_x[0] > 0 && quad.axis_y[1] > 0;
}

HudRegion source_region(const Quad& quad, const Image& image) {
  return {static_cast<int>(std::lround(quad.uv[0] * static_cast<float>(image.width))),
          static_cast<int>(std::lround(quad.uv[1] * static_cast<float>(image.height))),
          static_cast<int>(std::lround(quad.uv[2] * static_cast<float>(image.width))),
          static_cast<int>(std::lround(quad.uv[3] * static_cast<float>(image.height)))};
}
}

struct RetainedSurface::State {
  Image image;
  std::uint64_t revision{};
  HudRegion dirty{};
  bool dirty_pending{};
};

namespace {
void mark_dirty(RetainedSurface::State& state, HudRegion region) {
  state.dirty = state.dirty_pending ? unite(state.dirty, region) : region;
  state.dirty_pending = true;
  state.revision = next_revision();
}

void set_full_image(RetainedSurface::State& state, const Image& image) {
  state.image = image;
  state.dirty_pending = false;
  mark_dirty(state, {0, 0, image.width, image.height});
}

void copy_image_region(RetainedSurface::State& state, const Image& image, int x, int y, int width,
                       int height) {
  if (state.image.width != image.width || state.image.height != image.height ||
      state.image.pixels.empty()) {
    set_full_image(state, image);
    return;
  }
  state.image.pixel_scale = image.pixel_scale;
  const auto row_bytes = static_cast<std::size_t>(width) * 4;
  for (int row = y; row < y + height; ++row) {
    const auto offset = (static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width) +
                         static_cast<std::size_t>(x)) *
                        4;
    std::copy_n(image.pixels.data() + offset, row_bytes, state.image.pixels.data() + offset);
  }
  mark_dirty(state, {x, y, width, height});
}
}

RetainedSurface::RetainedSurface() : state_(std::make_shared<State>()) {}

void RetainedSurface::update(const Image& image) {
  validate_image(image);
  set_full_image(*state_, image);
}

void RetainedSurface::update_region(const Image& image, int x, int y, int width, int height) {
  validate_image(image);
  if (x < 0 || y < 0 || width < 1 || height < 1 || x > image.width - width ||
      y > image.height - height) {
    throw std::invalid_argument("Invalid UI texture upload");
  }
  copy_image_region(*state_, image, x, y, width, height);
}

Quad RetainedSurface::quad(Vec2 origin, Vec2 axis_x, Vec2 axis_y, int x, int y, int width,
                           int height) const {
  if (state_->image.width < 1 || state_->image.height < 1) {
    throw std::logic_error("RmlUi surface was not updated");
  }
  if (width == 0) {
    width = state_->image.width;
  }
  if (height == 0) {
    height = state_->image.height;
  }
  Quad quad;
  quad.origin = {static_cast<float>(origin.x), static_cast<float>(origin.y)};
  quad.axis_x = {static_cast<float>(axis_x.x), static_cast<float>(axis_x.y)};
  quad.axis_y = {static_cast<float>(axis_y.x), static_cast<float>(axis_y.y)};
  quad.kind = 5;
  quad.page = -1;
  quad.owner = state_;
  quad.uv = {static_cast<float>(x) / static_cast<float>(state_->image.width),
             static_cast<float>(y) / static_cast<float>(state_->image.height),
             static_cast<float>(width) / static_cast<float>(state_->image.width),
             static_cast<float>(height) / static_cast<float>(state_->image.height)};
  return quad;
}

struct RetainedUi::Impl {
  Impl(const FontBook& fonts, const HudArt* art) : host(fonts, art) {
    document = host.load("retained");
    if (!document) {
      throw std::runtime_error("The retained UI document did not load");
    }
    container = document->GetElementById("retained");
    if (!container) {
      throw std::runtime_error("The retained UI document has no retained container");
    }
  }

  ~Impl() { host.close(document); }

  enum class SlotType { none, image, solid };

  struct Slot {
    Rml::Element* element{};
    SlotType type{SlotType::none};
    const RetainedSurface::State* state{};
    std::uint64_t revision{};
    HudRegion source{};
    bool source_set{};
    std::string left;
    std::string top;
    std::string width;
    std::string height;
    std::string background;
    std::string display;
    // Raw values behind the cached strings: unchanged values skip formatting.
    std::array<float, 4> box{};
    bool boxed{};
    std::array<float, 4> tint{};
    bool tinted{};
  };

  Slot& slot(std::size_t index, SlotType type) {
    if (index == slots.size()) {
      slots.emplace_back();
    }
    auto& result = slots.at(index);
    if (result.type != type) {
      auto replacement = document->CreateElement(type == SlotType::image ? "native-image" : "div");
      if (!replacement) {
        throw std::runtime_error("Cannot create retained UI element");
      }
      auto* raw = replacement.get();
      raw->SetId("retained-" + std::to_string(index));
      raw->SetProperty("position", "absolute");
      raw->SetProperty("display", "none");
      if (result.element) {
        container->ReplaceChild(std::move(replacement), result.element);
      } else {
        container->AppendChild(std::move(replacement));
      }
      result = Slot{};
      result.element = raw;
      result.type = type;
    }
    return result;
  }

  static void property(Slot& slot, std::string& cached, const char* name, std::string_view value) {
    if (cached == value) {
      return;
    }
    cached.assign(value);
    slot.element->SetProperty(name, cached);
  }

  void place(Slot& slot, const Quad& quad) {
    const std::array<float, 4> box{quad.origin[0], quad.origin[1], quad.axis_x[0], quad.axis_y[1]};
    if (!slot.boxed || slot.box != box) {
      property(slot, slot.left, "left", px(box[0]));
      property(slot, slot.top, "top", px(box[1]));
      property(slot, slot.width, "width", px(box[2]));
      property(slot, slot.height, "height", px(box[3]));
      slot.box = box;
      slot.boxed = true;
    }
    property(slot, slot.display, "display", "block");
  }

  RmlHost host;
  Rml::ElementDocument* document{};
  Rml::Element* container{};
  std::vector<Slot> slots;
  // Reused across frames; a state may repeat, which is harmless as its flag is only cleared.
  std::vector<RetainedSurface::State*> dirty_states;
  bool visible{};
};

RetainedUi::RetainedUi(const FontBook& fonts, const HudArt* art)
    : impl_(std::make_unique<Impl>(fonts, art)) {}

RetainedUi::~RetainedUi() = default;

RmlHost& RetainedUi::host() {
  return impl_->host;
}

std::vector<Quad> RetainedUi::compose(std::span<const Quad> quads, std::int32_t time_ms,
                                      const Viewport& viewport) {
  auto& impl = *impl_;
  impl.host.resize(viewport);

  std::vector<Quad> native;
  impl.dirty_states.clear();
  std::size_t used{};
  bool has_retained = false;

  for (const auto& quad : quads) {
    const auto kind = static_cast<int>(std::lround(quad.kind));
    if (kind == 5 && quad.page == -1) {
      auto* state = static_cast<const RetainedSurface::State*>(quad.owner.get());
      if (!state || !axis_aligned(quad)) {
        throw std::logic_error("RmlUi retained images must be axis-aligned with positive axes");
      }

      auto& slot = impl.slot(used, Impl::SlotType::image);
      impl.place(slot, quad);
      const bool identity_changed = slot.state != state;
      if (identity_changed || slot.revision != state->revision) {
        const HudRegion full{0, 0, state->image.width, state->image.height};
        const HudRegion dirty = identity_changed || !state->dirty_pending ? full : state->dirty;
        set_native_image(*slot.element, state->image, state->revision,
                         std::span<const HudRegion>(&dirty, 1));
        slot.state = state;
        slot.revision = state->revision;
      }

      const auto source = source_region(quad, state->image);
      if (!slot.source_set || slot.source.x != source.x || slot.source.y != source.y ||
          slot.source.width != source.width || slot.source.height != source.height) {
        set_native_source(*slot.element, source);
        slot.source = source;
        slot.source_set = true;
      }
      set_native_tint(*slot.element, quad.multiply);
      if (state->dirty_pending) {
        impl.dirty_states.push_back(const_cast<RetainedSurface::State*>(state));
      }
      ++used;
      has_retained = true;
      continue;
    }

    if (kind == 2 && axis_aligned(quad)) {
      auto& slot = impl.slot(used, Impl::SlotType::solid);
      impl.place(slot, quad);
      if (!slot.tinted || slot.tint != quad.multiply) {
        Impl::property(slot, slot.background, "background-color", rgba(quad.multiply));
        slot.tint = quad.multiply;
        slot.tinted = true;
      }
      ++used;
      has_retained = true;
      continue;
    }
    native.push_back(quad);
  }

  for (std::size_t index = used; index < impl.slots.size(); ++index) {
    auto& slot = impl.slots[index];
    Impl::property(slot, slot.display, "display", "none");
  }

  if (impl.visible != has_retained) {
    if (has_retained) {
      impl.host.show(*impl.document, false, false);
    } else {
      impl.host.hide(*impl.document);
    }
    impl.visible = has_retained;
  }

  impl.host.update(time_ms);
  for (auto* state : impl.dirty_states) {
    state->dirty_pending = false;
  }
  return native;
}

void RetainedUi::render(int drawable_width, int drawable_height) {
  impl_->host.render(drawable_width, drawable_height);
}

}
