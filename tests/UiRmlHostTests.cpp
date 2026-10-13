#include <GL/glew.h>
// The shared UI foundation's graphics half (RmlHost) with real fonts, HUD art and GL. One document exercises a
// painter-decorated panel, FontBook text, a text field, a modal document and an overflow-clipped list, driven through a
// ui::Stack exactly as the client drives it. Checks:
// - retained invalidation: once drawn, unchanged frames rasterize, upload and regenerate nothing; one changed label
//   rasterizes one string and repaints no panel; a resize re-rasterizes once and is then static again;
// - input: hit-testing (body is not a hit), capture on press, focus and text entry, keyboard editing, modal blocking;
// - clipping: content scrolled out of an overflow:hidden box is not drawn outside it (pixel readback);
// - lifetime: caches released and rebuilt, the host destroyed with documents and listeners alive and re-created on the
//   same GL context, a second concurrent host refused; no GL errors and no RmlUi errors throughout.
#include "realm/platform/PlatformSdl.hpp"
#include "realm/lib/ui/RmlHost.hpp"
#include "realm/lib/ui/Stack.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <SDL.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace realm;
using namespace realm::ui;
int checks{};
void check(bool ok, const std::string& why) {
  ++checks;
  if (!ok) throw std::runtime_error(why);
}
void gl_clean(const char* phase) {
  const auto error = glGetError();
  check(error == GL_NO_ERROR, std::string("GL error ") + std::to_string(error) + " after " + phase);
}
constexpr int width = 800, height = 600;
const std::string document = R"(<rml><head><title>q10</title>
<link type="text/rcss" href="flash.rcss"/><link type="text/rcss" href="text.rcss"/>
<style>
body{background-color:transparent}
#panel{position:absolute;left:20dp;top:20dp;width:200dp;height:100dp;decorator:paint(cut-rect 0x363636 0xffffff 4)}
#title{position:absolute;left:30dp;top:28dp;white-space:nowrap}
#list{position:absolute;left:300dp;top:20dp;width:200dp;height:100dp;overflow:hidden}
#tall{width:200dp;height:400dp;background-color:#ff0000}
#name{position:absolute;left:30dp;top:200dp;width:200dp;height:30dp;background-color:#333;color:#b3b3b3}
#native,#crop{display:block;position:absolute;left:550dp;width:60dp;height:20dp}
#native{top:20dp}#crop{top:60dp;width:20dp;height:20dp}
</style></head><body>
<div id="panel"></div><div id="title" class="t16b">Static title</div>
<div id="list"><div id="tall"></div></div>
<native-image id="native"></native-image><native-image id="crop"></native-image>
<input type="text" id="name" maxlength="20"/>
</body></rml>)";
const std::string modal_document =
    R"(<rml><head><title>modal</title><link type="text/rcss" href="flash.rcss"/><link type="text/rcss" href="text.rcss"/>
<style>body{background-color:#00000080}#ok{position:absolute;left:350dp;top:400dp;width:100dp;height:30dp}</style></head>
<body><div id="ok" class="button t16b">OK</div></body></rml>)";

struct Counts {
  std::uint64_t text{}, paint{}, uploads{}, meshes{}, native_uploads{}, native_bytes{}, native_meshes{};
};
Counts counts(const RmlHost& host) {
  const auto s = host.stats();
  return {s.text_rasterizations,  s.painter_rasterizations,    s.texture_uploads,    s.text_meshes,
          s.native_image_uploads, s.native_image_upload_bytes, s.native_image_meshes};
}
Counts operator-(Counts a, Counts b) {
  return {a.text - b.text,
          a.paint - b.paint,
          a.uploads - b.uploads,
          a.meshes - b.meshes,
          a.native_uploads - b.native_uploads,
          a.native_bytes - b.native_bytes,
          a.native_meshes - b.native_meshes};
}
std::int32_t clock_ms{};
void frame(RmlHost& host, int w = width, int h = height) {
  clock_ms += 16;
  host.update(clock_ms);
  glViewport(0, 0, width, height);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  host.render(w, h);
}
std::array<unsigned char, 4> pixel(int x, int y) {
  std::array<unsigned char, 4> rgba{};
  glReadPixels(x, height - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  return rgba;
}
Event pointer(EventKind kind, double x, double y) {
  Event e;
  e.kind = kind;
  e.point = {x, y};
  return e;
}
Event key(SDL_Keycode code) {
  Event e;
  e.kind = EventKind::key_down;
  e.key = code;
  return e;
}

void run(const FontBook& fonts, const HudArt& art) {
  RmlHost host(fonts, &art);
  bool refused = false;
  try {
    RmlHost second(fonts, &art);
  } catch (const std::logic_error&) {
    refused = true;
  }
  check(refused, "a second concurrent host is refused");
  host.resize({width, height, width, height});
  auto* doc = host.load_from_memory(document, "q10.rml");
  auto* modal = host.load_from_memory(modal_document, "q10-modal.rml");
  check(doc && modal, "documents load from memory with embedded flash.rcss/text.rcss");
  host.show(*doc, false, false);
  Image native_image{3, 1, std::vector<unsigned char>{255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 255}, 1};
  auto* native = doc->GetElementById("native");
  auto* crop = doc->GetElementById("crop");
  check(native && crop, "native-image elements load through the custom instancer");
  set_native_image(*native, native_image, 1);
  set_native_image(*crop, native_image, 1);
  set_native_source(*crop, {2, 0, 1, 1});
  auto image_stats = host.stats();
  check(image_stats.native_image_bytes == 12 && image_stats.native_image_uploads == 1 &&
            image_stats.native_image_upload_bytes == 12,
        "two crops share one synchronously uploaded native texture");
  Stack stack;
  stack.add(host.layer());
  for (int i = 0; i < 3; ++i) frame(host);
  gl_clean("first frames");
  const auto first = host.stats();
  check(first.text_rasterizations > 0 && first.painter_rasterizations == 1 && first.texture_uploads >= 2,
        "first frames rasterize text and paint the panel once");
  check(first.native_image_meshes == 2, "each native-image element builds one retained crop mesh");

  // Retained: unchanged frames do no text, painter or upload work and regenerate no text geometry.
  auto before = counts(host);
  for (int i = 0; i < 30; ++i) frame(host);
  auto delta = counts(host) - before;
  check(delta.text == 0 && delta.paint == 0 && delta.uploads == 0 && delta.meshes == 0 && delta.native_uploads == 0 &&
            delta.native_meshes == 0,
        "static frames are free: text " + std::to_string(delta.text) + " paint " + std::to_string(delta.paint) +
            " uploads " + std::to_string(delta.uploads) + " meshes " + std::to_string(delta.meshes));
  // Hover over a non-interactive panel changes nothing drawn.
  stack.dispatch(pointer(EventKind::pointer_move, 100, 60));
  before = counts(host);
  for (int i = 0; i < 5; ++i) frame(host);
  delta = counts(host) - before;
  check(delta.text == 0 && delta.paint == 0 && delta.uploads == 0, "hover over static content is free");
  // A changed label: one string rasterized and uploaded, no panel repainted.
  doc->GetElementById("title")->SetInnerRML("Changed title");
  before = counts(host);
  frame(host);
  delta = counts(host) - before;
  check(delta.text == 1 && delta.paint == 0 && delta.uploads == 1,
        "one changed label costs one rasterization: text " + std::to_string(delta.text) + " paint " +
            std::to_string(delta.paint) + " uploads " + std::to_string(delta.uploads));
  before = counts(host);
  for (int i = 0; i < 10; ++i) frame(host);
  delta = counts(host) - before;
  check(delta.text == 0 && delta.uploads == 0, "static again after the change");

  // Painted panel and clipping, read back from the framebuffer.
  frame(host);
  const auto panel = pixel(100, 70), list = pixel(400, 60), below = pixel(400, 200), body = pixel(700, 500);
  check(panel[0] == 0x36 && panel[1] == 0x36 && panel[2] == 0x36, "cut-rect painter fills the panel");
  check(list[0] == 255 && list[1] == 0 && list[2] == 0, "list content visible inside its box");
  check(below[0] == 0 && below[1] == 0 && below[2] == 0,
        "overflow:hidden clips the 400dp child at the box's 100dp height");
  check(body[0] == 0 && body[1] == 0 && body[2] == 0, "transparent body draws nothing");
  const auto red = pixel(555, 30), green = pixel(575, 30), blue = pixel(595, 30), cropped = pixel(560, 70);
  check(red[0] > 240 && red[1] < 8 && red[2] < 8, "native image draws the first source texel");
  check(green[1] > 115 && green[1] < 140 && green[0] < 8 && green[2] < 8,
        "native image uploads straight alpha as premultiplied green");
  check(blue[2] > 240 && blue[0] < 8 && blue[1] < 8 && cropped[2] > 240,
        "native source crop draws only its selected texel");
  gl_clean("readback");
  set_native_tint(*crop, {1, 1, 1, .5F});
  frame(host);
  const auto tinted = pixel(560, 70);
  check(tinted[2] > 115 && tinted[2] < 140, "native image tint multiplies alpha without rebuilding its texture");
  set_native_tint(*crop, {1, 1, 1, 1});
  frame(host);

  // Dirty uploads clip safely and copy only the selected source region before premultiplying it.
  native_image.pixels[0] = 255;
  native_image.pixels[1] = 255;
  native_image.pixels[2] = 0;
  const HudRegion clipped_dirty{-3, 0, 4, 1};
  before = counts(host);
  set_native_image(*native, native_image, 2, std::span<const HudRegion>(&clipped_dirty, 1));
  delta = counts(host) - before;
  check(delta.native_uploads == 1 && delta.native_bytes == 4 && host.stats().native_image_bytes == 12,
        "clipped dirty upload transfers only one pixel");
  frame(host);
  const auto changed = pixel(555, 30);
  check(changed[0] > 240 && changed[1] > 240 && changed[2] < 8, "dirty native pixel is visible synchronously");

  // A new image size allocates and fills one new shared texture even when a crop was previously selected.
  native_image.width = 2;
  native_image.height = 2;
  native_image.pixels = {0, 0, 255, 255, 255, 255, 0, 255, 0, 255, 255, 255, 255, 0, 255, 255};
  before = counts(host);
  set_native_image(*native, native_image, 3);
  set_native_image(*crop, native_image, 3);
  set_native_source(*crop, {0, 0, 2, 2});
  delta = counts(host) - before;
  check(delta.native_uploads == 1 && delta.native_bytes == 16 && host.stats().native_image_bytes == 16,
        "resizing replaces the texture and uploads the full image once");
  frame(host);
  check(pixel(555, 30)[2] > 240, "resized native texture draws after its mesh refreshes");

  // Input through the stack: the body is not a hit, controls are; a press captures; focus brings text entry.
  check(!host.layer().covers({700, 500}) && stack.pointer_free({700, 500}),
        "body is not a hit; the world gets the pointer");
  check(host.layer().covers({100, 60}) && !stack.pointer_free({100, 60}), "panel occludes the world");
  check(!stack.dispatch(pointer(EventKind::pointer_down, 700, 500)).consumed,
        "click on empty body passes to the world");
  stack.dispatch(pointer(EventKind::pointer_up, 700, 500));
  auto d = stack.dispatch(pointer(EventKind::pointer_down, 100, 215));
  check(d.consumed && stack.capture() == &host.layer(), "press on the field captures");
  stack.dispatch(pointer(EventKind::pointer_up, 100, 215));
  frame(host);
  check(stack.wants_text() && !stack.keys_free(), "focused field takes the keyboard");
  Event typed;
  typed.kind = EventKind::text;
  typed.text =
      "R\xC3\xA9"
      "alm";
  check(stack.dispatch(typed).consumed, "text consumed by the field");
  stack.dispatch(key(SDLK_BACKSPACE));
  frame(host);
  auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(doc->GetElementById("name"));
  check(input && input->GetValue() ==
                     "R\xC3\xA9"
                     "al",
        "UTF-8 typing and backspace edit the value: '" + (input ? input->GetValue() : std::string("?")) + "'");
  // A key with no text focus passes to the game bindings.
  stack.dispatch(pointer(EventKind::pointer_down, 700, 500));
  stack.dispatch(pointer(EventKind::pointer_up, 700, 500));
  frame(host);
  check(!stack.wants_text() && !stack.dispatch(key(SDLK_w)).consumed,
        "blurred field releases the keyboard to the world");

  // Modal document: the layer turns modal, swallows input outside itself and blocks the world.
  host.show(*modal, true);
  frame(host);
  check(stack.modal() == &host.layer() && !stack.pointer_free({700, 100}), "modal document makes the layer modal");
  check(stack.dispatch(key(SDLK_w)).consumed, "modal swallows keys");
  host.hide(*modal);
  frame(host);
  check(!stack.modal(), "hidden modal releases input");

  // Resize: one re-rasterization at the new density, then static again.
  Event resized;
  resized.kind = EventKind::resized;
  resized.viewport = {width, height, width * 2, height * 2};
  stack.dispatch(resized);
  check(host.scale() == 2, "resize sets the dp ratio");
  before = counts(host);
  frame(host, width * 2, height * 2);
  delta = counts(host) - before;
  check(delta.text > 0 && delta.paint == 1, "resize re-rasterizes text and repaints the panel once");
  before = counts(host);
  for (int i = 0; i < 10; ++i) frame(host, width * 2, height * 2);
  delta = counts(host) - before;
  check(delta.text == 0 && delta.paint == 0 && delta.uploads == 0 && delta.meshes == 0, "static after resize");
  resized.viewport = {width, height, width, height};
  stack.dispatch(resized);
  frame(host);

  // Released caches (map change) are rebuilt on demand.
  host.release_caches();
  check(host.stats().text_cache_entries == 0 && host.stats().painter_cache_entries == 0, "caches released");
  before = counts(host);
  frame(host);
  delta = counts(host) - before;
  check(delta.text > 0 && delta.paint == 1, "released caches re-rasterize on the next frame");
  frame(host);
  const auto end = pixel(100, 70);
  check(end[0] == 0x36, "panel drawn after release");
  // A listener whose element goes away with the document must not outlive it.
  bool clicked = false;
  host.listen(*doc->GetElementById("panel"), "click", [&](Rml::Event&) { clicked = true; });
  stack.dispatch(pointer(EventKind::pointer_down, 100, 60));
  stack.dispatch(pointer(EventKind::pointer_up, 100, 60));
  check(clicked, "listener receives the click");

  const auto retained_bytes = host.stats().native_image_bytes;
  Image malformed{1, 1, {}, 1};
  set_native_image(*native, malformed, 4);
  check(host.stats().native_image_bytes == retained_bytes, "invalid native pixels are ignored safely");
  host.close(doc);
  frame(host);
  check(host.stats().native_image_bytes == 0, "closing the document releases its native texture");
  doc = host.load_from_memory(document, "q10-reopened.rml");
  check(doc != nullptr, "native-image document reopens");
  host.show(*doc, false, false);
  native = doc->GetElementById("native");
  crop = doc->GetElementById("crop");
  before = counts(host);
  set_native_image(*native, native_image, 3);
  set_native_image(*crop, native_image, 3);
  set_native_source(*crop, {0, 0, 2, 2});
  frame(host);
  delta = counts(host) - before;
  check(delta.native_uploads == 1 && delta.native_bytes == 16 && host.stats().native_image_bytes == 16,
        "reopened document owns one freshly uploaded texture");
  check(host.stats().log_errors == 0 && host.stats().log_warnings == 0, "no RmlUi diagnostics");
  gl_clean("first host");
}  // destroyed with both documents and a listener alive
}
int main() {
  try {
    platform::WindowSession session;
    session.open("q10 host", width, height, true, 0);
    FontBook fonts;
    HudArt art;
    run(fonts, art);
    gl_clean("first host teardown");
    // Reconnect: a fresh host on the same context behaves the same way.
    run(fonts, art);
    gl_clean("second host teardown");
    std::cout << "ui rml host: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
