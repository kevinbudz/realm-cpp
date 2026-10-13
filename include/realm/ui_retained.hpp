#pragma once
#include "realm/hud.hpp"
#include "realm/renderer.hpp"
#include "realm/ui_rml.hpp"
#include <memory>
#include <span>
#include <vector>

namespace realm::ui {

// A UI surface that retains a CPU image snapshot for RmlUi composition. It performs no GL upload of its own.
class RetainedSurface {
 public:
  struct State;

  RetainedSurface();
  RetainedSurface(const RetainedSurface&) = delete;
  RetainedSurface& operator=(const RetainedSurface&) = delete;

  void update(const Image& image);
  void update_region(const Image& image, int x, int y, int width, int height);
  [[nodiscard]] Quad quad(Vec2 origin, Vec2 axis_x, Vec2 axis_y, int x = 0, int y = 0, int width = 0,
                          int height = 0) const;

 private:
  std::shared_ptr<State> state_;
};

// Composes retained surfaces and solid backdrops through the shared RmlHost. Input remains owned by ui::Stack layers.
class RetainedUi {
 public:
  RetainedUi(const FontBook& fonts, const HudArt* art);
  ~RetainedUi();
  RetainedUi(const RetainedUi&) = delete;
  RetainedUi& operator=(const RetainedUi&) = delete;

  [[nodiscard]] RmlHost& host();
  [[nodiscard]] std::vector<Quad> compose(std::span<const Quad> quads, std::int32_t time_ms, const Viewport& viewport);
  void render(int drawable_width, int drawable_height);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
