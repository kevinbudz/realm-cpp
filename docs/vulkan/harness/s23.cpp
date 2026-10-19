// Scratch harness (not part of the repo): S2 frames, presentation, capture, resize; S3 atlas uploads,
// layer and surface readback, layer resize. Links the same libraries as realm_client.
#include "realm/platform/PlatformSdl.hpp"
#include "realm/stage3D/Atlas.hpp"
#include "realm/stage3D/GraphicsApi.hpp"
#include "realm/stage3D/Renderer.hpp"
#include "stage3D/vk/VkResources.hpp"
#include "stage3D/vk/VkFrame.hpp"
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <vector>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
  std::cout << (ok ? "PASS " : "FAIL ") << what << '\n';
  if (!ok) {
    ++failures;
  }
}

realm::gfx::WindowSurface window_surface(realm::platform::WindowSession& session) {
  return {[&session] { return session.vulkan_instance_proc(); },
          [&session] { return session.vulkan_instance_extensions(); },
          [&session](void* instance) { return session.vulkan_surface(instance); }};
}

unsigned char expect(float channel) {
  return static_cast<unsigned char>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
}
}  // namespace

int main(int argc, char** argv) {
  const std::string flash = argc > 1 ? argv[1] : "realm-client";
  realm::platform::WindowSession session;
  session.open(realm::gfx::Api::vulkan, "s23 harness", 320, 240, false, 1);
  realm::gfx::select_api(realm::gfx::Api::vulkan);
  realm::initialise_graphics(window_surface(session));
  std::cout << "GPU " << realm::graphics_info().renderer << " | " << realm::graphics_info().version << '\n';

  {  // the renderer and the atlas die before the graphics API shuts down
  // S3: the real atlas uploads through the backend (create_layers, upload_layer, verify).
  realm::Atlas atlas{std::filesystem::path(flash)};
  check(atlas.texture() != 0, "atlas created with a handle");

  // S2: frames with clear colours; each frame is one draw plus finish (acquire, submit, present).
  realm::Renderer renderer(16);
  // "toggle" argument: enable presented capture through the API instead of REALM_VK_PRESENTED_COPY=1.
  const bool toggle = argc > 2 && std::string(argv[2]) == "toggle";
  if (toggle) {
    renderer.set_presented_capture(true);
  }
  int width = 0, height = 0;
  session.drawable_size(width, height);
  std::cout << "drawable " << width << "x" << height << '\n';
  const auto start = std::chrono::steady_clock::now();
  constexpr int frames = 300;
  for (int i = 0; i < frames; ++i) {
    const float shade = static_cast<float>(i % 7) / 7.0f;
    renderer.clear_color({shade, 0.25f, 0.5f, 1.0f});
    renderer.draw(atlas, {}, width, height, width);
    renderer.finish_frame();
    session.present();
  }
  const auto micros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
  std::cout << "frame mean " << micros / frames << " us over " << frames << " frames\n";

  // Capture of the last draw: every pixel is the clear colour (RGBA, top row first).
  renderer.clear_color({0.2f, 0.4f, 0.8f, 1.0f});
  renderer.draw(atlas, {}, width, height, width);
  const auto image = renderer.capture(width, height);
  check(image.width == width && image.height == height && image.pixels.size() == static_cast<std::size_t>(width * height * 4),
        "capture has the drawable's size");
  std::size_t wrong = 0;
  for (std::size_t p = 0; p + 3 < image.pixels.size(); p += 4) {
    wrong += image.pixels[p] != expect(0.2f) || image.pixels[p + 1] != expect(0.4f) ||
             image.pixels[p + 2] != expect(0.8f) || image.pixels[p + 3] != expect(1.0f);
  }
  check(wrong == 0, "capture pixels equal the clear colour (" + std::to_string(wrong) + " differ)");
  renderer.finish_frame();
  session.present();
  // Presented capture: the last presented frame when REALM_VK_PRESENTED_COPY=1, black otherwise.
  const auto presented = renderer.capture(width, height, true);
  std::size_t same = 0;
  for (std::size_t p = 0; p + 3 < presented.pixels.size(); p += 4) {
    same += presented.pixels[p] == image.pixels[p] && presented.pixels[p + 1] == image.pixels[p + 1] && presented.pixels[p + 2] == image.pixels[p + 2];
  }
  std::cout << "presented capture matches the last frame in " << same << " of " << presented.pixels.size() / 4 << " pixels; first pixel " << int(presented.pixels[0]) << ',' << int(presented.pixels[1]) << ',' << int(presented.pixels[2]) << '\n';

  // Resize: the swapchain rebuilds at the new drawable size.
  SDL_SetWindowSize(session.window, 400, 300);
  for (int i = 0; i < 20; ++i) {
    SDL_PumpEvents();
  }
  session.drawable_size(width, height);
  renderer.clear_color({0.9f, 0.1f, 0.1f, 1.0f});
  renderer.draw(atlas, {}, width, height, width);
  const auto resized = renderer.capture(width, height);
  wrong = 0;
  for (std::size_t p = 0; p + 3 < resized.pixels.size(); p += 4) {
    wrong += resized.pixels[p] != expect(0.9f) || resized.pixels[p + 1] != expect(0.1f);
  }
  check(resized.width == width && resized.height == height && wrong == 0,
        "resized swapchain renders at " + std::to_string(width) + "x" + std::to_string(height));
  renderer.finish_frame();
  session.present();

  }
  // S3 readback: a layer array with a known pattern, a layer resize that keeps layer 1, and a surface.
  auto& backend = realm::gfx::backend();
  const auto layers = backend.create_layers(64, 2);
  std::vector<std::byte> pattern(8 * 8 * 4);
  for (std::size_t i = 0; i < pattern.size(); ++i) {
    pattern[i] = static_cast<std::byte>((i * 37 + 11) & 0xff);
  }
  backend.upload_layer(layers, 1, 4, 4, 8, 8, pattern.data());
  std::vector<std::byte> out;
  realm::gfx::vk::resources().read_layer(layers, 1, 4, 4, 8, 8, out);
  check(out == pattern, "layer upload reads back exactly");
  realm::gfx::vk::frames().finish();

  const auto grown = backend.resize_layers(layers, 64, 3, 2, true);
  backend.release_texture(layers);
  realm::gfx::vk::resources().read_layer(grown, 1, 4, 4, 8, 8, out);
  check(out == pattern, "resize keeps the copied layer");
  realm::gfx::vk::resources().read_layer(grown, 2, 0, 0, 64, 64, out);
  bool cleared = true;
  for (const auto b : out) {
    cleared &= b == std::byte{0};
  }
  check(cleared, "resize clears the new layer");
  realm::gfx::vk::frames().finish();

  const auto surface = backend.create_surface(false);
  backend.allocate_surface(surface, 16, 8);
  std::vector<std::byte> surface_pixels(16 * 8 * 4);
  for (std::size_t i = 0; i < surface_pixels.size(); ++i) {
    surface_pixels[i] = static_cast<std::byte>((i * 13 + 5) & 0xff);
  }
  backend.upload_surface(surface, 0, 0, 16, 8, surface_pixels.data());
  realm::gfx::vk::resources().read_layer(surface, 0, 0, 0, 16, 8, out);
  check(out == surface_pixels, "surface upload reads back exactly");
  realm::gfx::vk::frames().finish();
  backend.release_texture(surface);

  std::vector<std::byte> buffer_bytes(1000);
  for (std::size_t i = 0; i < buffer_bytes.size(); ++i) {
    buffer_bytes[i] = static_cast<std::byte>(i & 0xff);
  }
  const auto buffer = backend.create_buffer(buffer_bytes);
  check(buffer != 0, "static buffer created");
  backend.release_buffer(buffer);

  realm::shutdown_graphics();
  std::cout << (failures ? "FAILURES " : "ALL PASS ") << failures << '\n';
  return failures ? 1 : 0;
}
