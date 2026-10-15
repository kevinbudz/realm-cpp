#include <GL/glew.h>
#include "realm/platform/PlatformSdl.hpp"
#include "realm/lib/ui/RetainedUi.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace realm;
using namespace realm::ui;
constexpr int width = 800, height = 600;
int checks{};

void check(bool ok, const std::string& why) {
  ++checks;
  if (!ok) {
    throw std::runtime_error(why);
  }
}

Image solid_image(int w, int h, std::array<unsigned char, 4> color) {
  Image image{
      w, h,
      std::vector<unsigned char>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4), 1};
  for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
    std::copy(color.begin(), color.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(i));
  }
  return image;
}

void set_pixel(Image& image, int x, int y, std::array<unsigned char, 4> color) {
  const auto at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                   static_cast<std::size_t>(x)) *
                  4;
  std::copy(color.begin(), color.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(at));
}

std::array<unsigned char, 4> pixel(int x, int y) {
  std::array<unsigned char, 4> rgba{};
  glReadPixels(x, height - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  return rgba;
}

void near(unsigned char value, int expected, int tolerance, const std::string& why) {
  check(std::abs(static_cast<int>(value) - expected) <= tolerance,
        why + ": got " + std::to_string(value));
}

bool same_quad(const Quad& a, const Quad& b) {
  return a.origin == b.origin && a.axis_x == b.axis_x && a.axis_y == b.axis_y && a.uv == b.uv &&
         a.page == b.page && a.multiply == b.multiply && a.offset == b.offset && a.kind == b.kind &&
         a.uv_offset == b.uv_offset && a.owner.get() == b.owner.get() && a.texture == b.texture;
}

struct Counts {
  std::uint64_t text{}, paint{}, uploads{}, upload_bytes{}, meshes{};
  std::uint64_t image_uploads{}, image_upload_bytes{}, image_meshes{};
};

Counts counts(const RmlHost& host) {
  const auto s = host.stats();
  return {s.text_rasterizations,
          s.painter_rasterizations,
          s.texture_uploads,
          s.texture_upload_bytes,
          s.text_meshes,
          s.native_image_uploads,
          s.native_image_upload_bytes,
          s.native_image_meshes};
}

Counts operator-(Counts a, Counts b) {
  return {a.text - b.text,
          a.paint - b.paint,
          a.uploads - b.uploads,
          a.upload_bytes - b.upload_bytes,
          a.meshes - b.meshes,
          a.image_uploads - b.image_uploads,
          a.image_upload_bytes - b.image_upload_bytes,
          a.image_meshes - b.image_meshes};
}

std::vector<Quad> frame(RetainedUi& retained, std::span<const Quad> quads, const Viewport& viewport,
                        std::int32_t time, std::array<float, 4> clear = {0, 0, 0, 1}) {
  glViewport(0, 0, width, height);
  glClearColor(clear[0], clear[1], clear[2], clear[3]);
  glClear(GL_COLOR_BUFFER_BIT);
  auto native = retained.compose(quads, time, viewport);
  retained.render(width, height);
  return native;
}

void run(const FontBook& fonts, const HudArt& art) {
  RetainedUi retained(fonts, &art);
  RetainedSurface red;
  RetainedSurface blue;
  auto red_pixels = solid_image(4, 2, {255, 0, 0, 255});
  auto blue_pixels = solid_image(4, 2, {20, 240, 20, 255});
  for (int y = 0; y < 2; ++y) {
    for (int x = 2; x < 4; ++x) {
      set_pixel(blue_pixels, x, y, {0, 0, 255, 128});
    }
  }
  red.update(red_pixels);
  blue.update(blue_pixels);

  SurfaceTexture gl_surface;
  gl_surface.update(solid_image(2, 2, {80, 80, 80, 255}));
  const auto gl_surface_quad = gl_surface.quad({3, 4}, {2, 0}, {0, 2});
  check(gl_surface_quad.kind == 5 && gl_surface_quad.texture > 0 &&
            gl_surface_quad.uv == std::array<float, 4>{0, 0, 1, 1},
        "a GL surface texture keeps its quad and texture");

  Quad world;
  world.origin = {7, 8};
  world.axis_x = {9, 0};
  world.axis_y = {0, 10};
  world.kind = 0;
  Quad backdrop;
  backdrop.origin = {20, 20};
  backdrop.axis_x = {45, 0};
  backdrop.axis_y = {0, 45};
  backdrop.kind = 2;
  backdrop.multiply = {0, 0, 0, .5F};
  auto red_quad = red.quad({10, 10}, {55, 0}, {0, 55});
  auto blue_quad = blue.quad({30, 30}, {20, 0}, {0, 20}, 2, 0, 2, 2);
  const auto viewport = Viewport{width, height, width, height};
  retained.host().reset_stats();

  const std::vector<Quad> ordered{red_quad, world, backdrop, gl_surface_quad, blue_quad};
  auto remaining = frame(retained, ordered, viewport, 16);
  check(remaining.size() == 2 && same_quad(remaining[0], world) &&
            same_quad(remaining[1], gl_surface_quad),
        "compose passes world and GL surface quads through in order");
  const auto red_only = pixel(15, 15);
  check(red_only == std::array<unsigned char, 4>{255, 0, 0, 255},
        "the selected unoccluded image pixel stays exact red");
  const auto dimmed = pixel(25, 35);
  near(dimmed[0], 127, 3, "solid backdrop composites over red");
  check(dimmed[1] < 5 && dimmed[2] < 5, "backdrop keeps the red image channels");
  const auto cropped = pixel(35, 35);
  check(cropped[2] > cropped[0] && cropped[0] < 100 && cropped[1] < 8,
        "cropped half-alpha blue source draws above the backdrop");

  auto changed_blue = blue_pixels;
  set_pixel(changed_blue, 3, 0, {0, 255, 0, 128});
  set_pixel(changed_blue, 2, 1, {0, 255, 0, 128});
  blue.update_region(changed_blue, 3, 0, 1, 1);
  blue.update_region(changed_blue, 2, 1, 1, 1);
  const auto before_update = counts(retained.host());
  frame(retained, ordered, viewport, 32);
  const auto update_delta = counts(retained.host()) - before_update;
  check(update_delta.image_uploads == 1 && update_delta.image_upload_bytes == 16 &&
            update_delta.upload_bytes == 16,
        "two dirty pixels upload only their 2x2 union");
  const auto changed_pixel = pixel(45, 35);
  check(changed_pixel[1] > 80 && changed_pixel[2] < 10,
        "the dirty crop receives its changed green pixel");

  const auto stable = counts(retained.host());
  for (int i = 0; i < 30; ++i) {
    frame(retained, ordered, viewport, 48 + i * 16);
  }
  const auto idle = counts(retained.host()) - stable;
  check(idle.text == 0 && idle.paint == 0 && idle.uploads == 0 && idle.upload_bytes == 0 &&
            idle.meshes == 0 && idle.image_uploads == 0 && idle.image_upload_bytes == 0 &&
            idle.image_meshes == 0,
        "unchanged retained frames do no rasterization, upload or mesh work");

  const std::vector<Quad> blue_then_red{blue_quad, backdrop, red_quad};
  const auto before_reorder = counts(retained.host());
  frame(retained, blue_then_red, viewport, 560);
  const auto red_on_top = pixel(35, 35);
  check(red_on_top == std::array<unsigned char, 4>{255, 0, 0, 255},
        "changing source order puts the red image above the blue image");
  const auto reorder_delta = counts(retained.host()) - before_reorder;
  check(reorder_delta.image_uploads == 0 && reorder_delta.image_upload_bytes == 0,
        "changing painter order reuses the same image revisions");
  const auto before_restore = counts(retained.host());
  frame(retained, ordered, viewport, 576);
  const auto blue_on_top = pixel(35, 35);
  check(blue_on_top[2] > blue_on_top[0], "restoring painter order restores the blue top image");
  const auto restore_delta = counts(retained.host()) - before_restore;
  check(restore_delta.image_uploads == 0 && restore_delta.image_upload_bytes == 0,
        "restoring painter order keeps cached source revisions");

  auto resized_red = solid_image(3, 3, {255, 255, 0, 255});
  auto partial_old = red_pixels;
  set_pixel(partial_old, 0, 0, {0, 255, 255, 255});
  red.update_region(partial_old, 0, 0, 1, 1);
  red.update_region(resized_red, 1, 1, 1, 1);
  auto resized_quad = red.quad({100, 100}, {30, 0}, {0, 30});
  const std::vector<Quad> resized_only{resized_quad};
  const auto before_resize = counts(retained.host());
  frame(retained, resized_only, viewport, 592);
  const auto resize_delta = counts(retained.host()) - before_resize;
  check(resize_delta.image_uploads == 1 && resize_delta.image_upload_bytes == 36,
        "source resize discards pending partial damage and uploads the full new image");
  near(pixel(110, 110)[0], 255, 0, "resized source keeps the exact drawable placement");
  near(pixel(110, 110)[1], 255, 0, "resized source color reaches the placed element");

  const std::vector<Quad> empty;
  frame(retained, empty, viewport, 608, {0, 1, 0, 1});
  const auto hidden = pixel(35, 35);
  check(hidden[0] == 0 && hidden[1] == 255 && hidden[2] == 0,
        "hiding retained surfaces leaves the cleared framebuffer without stale content");
  const auto stats = retained.host().stats();
  check(stats.log_errors == 0 && stats.log_warnings == 0,
        "retained document renders without RmlUi diagnostics");
  check(glGetError() == GL_NO_ERROR, "retained GL operations complete without errors");
}
}

int main() {
  try {
    platform::WindowSession session;
    session.open("retained UI", width, height, true, 0);
    FontBook fonts;
    HudArt art;
    run(fonts, art);
    std::cout << "ui retained: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
