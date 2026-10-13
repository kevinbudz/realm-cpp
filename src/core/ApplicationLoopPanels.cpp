#include "ApplicationLoop.hpp"
#include "realm/util/Telemetry.hpp"
#include "realm/ui/fonts/FontBook.hpp"
#include "realm/hud/Hud.hpp"
#include "realm/ui/TradePanel.hpp"
#include "realm/ui/dialogs/InteractionDialog.hpp"
#include "realm/ui/tooltip/TextToolTip.hpp"
#include "realm/parameters/Parameters.hpp"
#include <SDL.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "ApplicationOptions.hpp"
#include "ApplicationState.hpp"

namespace realm::application {
void update_panel_pixels(ClientState& cs, Frame& f) {
  auto& dt = f.dt;
  auto& time = f.time;
  if (cs.dialog.visible()) {
    int dw{}, dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.dialog.update(cs.fonts, dw, dh)) cs.dialog_texture.update(cs.dialog.image());
    }
  }
  if (cs.members.visible()) {
    int dw{}, dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.members.update(cs.fonts, dw, dh, dt)) cs.members_texture.update(cs.members.image());
    }
  }
  if (cs.skins && cs.skins->visible()) {
    int dw{}, dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.skins->update(dw, dh, time)) cs.skin_texture.update(cs.skins->image());
    }
  }
  if (cs.options_screen.visible()) {
    int dw{}, dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.options_screen.update(cs.fonts, dw, dh, time)) cs.options_texture.update(cs.options_screen.image());
    }
  }
  if (cs.board.visible()) {
    int dw{}, dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.board.update(cs.fonts, dw, dh, dt)) cs.board_texture.update(cs.board.image());
    }
  }
}

void build_modal_overlays(ClientState& cs, const Options& o, Frame& f) {
  auto& time = f.time;
  auto& menus = f.menus;
  auto& width = f.width;
  auto& height = f.height;
  auto& overlay_storage = f.overlay_storage;
  auto& overlay = f.overlay;
  if (cs.interaction.visible() && !menus) {
    cs.fonts.set_pixel_scale(realm::InteractionDialog::scale(width, height));
    if (cs.interaction.render(cs.fonts)) cs.interaction_texture.update(cs.interaction.image());
    cs.fonts.set_pixel_scale(height / 600.0);
    const auto at = realm::InteractionDialog::position(width, height);
    const double scale = realm::InteractionDialog::scale(width, height);
    auto dim = realm::Quad{};
    dim.origin = {0, 0};
    dim.axis_x = {static_cast<float>(width), 0};
    dim.axis_y = {0, static_cast<float>(height)};
    dim.kind = 2;
    dim.multiply = {0, 0, 0, .55F};
    overlay_storage[overlay.size()] = dim;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    auto frame_quad = cs.interaction_texture.quad(at, {realm::InteractionDialog::width * scale, 0},
                                                  {0, realm::InteractionDialog::height * scale});
    frame_quad.kind = 5;
    overlay_storage[overlay.size()] = frame_quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
  if (cs.board.visible() && !menus) {
    if (cs.board.update(cs.fonts, width, height)) cs.board_texture.update(cs.board.image());
    if (cs.board.backdrop()) {
      auto dim = realm::Quad{};
      dim.origin = {0, 0};
      dim.axis_x = {static_cast<float>(width), 0};
      dim.axis_y = {0, static_cast<float>(height)};
      dim.kind = 2;
      dim.multiply = {0, 0, 0, .8F};
      overlay_storage[overlay.size()] = dim;
      overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    }
    const auto& image = cs.board.image();
    auto frame_quad =
        cs.board_texture.quad(cs.board.canvas_position(), {image.width * cs.board.scale() / image.pixel_scale, 0},
                              {0, image.height * cs.board.scale() / image.pixel_scale});
    overlay_storage[overlay.size()] = frame_quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
  if (cs.members.visible() && !menus) {
    if (cs.members.update(cs.fonts, width, height)) cs.members_texture.update(cs.members.image());
    auto quad = cs.members_texture.quad({0, 0}, {static_cast<double>(width), 0}, {0, static_cast<double>(height)});
    overlay_storage[overlay.size()] = quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
  if (cs.skins && cs.skins->visible() && !menus) {
    if (cs.skins->update(width, height, time)) {
      const auto r = cs.skins->change();
      cs.skin_texture.update_region(cs.skins->image(), r.x, r.y, r.width, r.height);
    }
    auto quad = cs.skin_texture.quad({0, 0}, {static_cast<double>(width), 0}, {0, static_cast<double>(height)});
    overlay_storage[overlay.size()] = quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
  if (cs.options_screen.visible() && !menus) {
    if (cs.options_screen.update(cs.fonts, width, height, time)) cs.options_texture.update(cs.options_screen.image());
    auto quad = cs.options_texture.quad({0, 0}, {static_cast<double>(width), 0}, {0, static_cast<double>(height)});
    overlay_storage[overlay.size()] = quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
  if (cs.perf_stats && o.world && !menus) {
    // Toggle Performance Stats: recent FPS, rolling frame-time percentiles and resources.
    if (cs.perf_dirty && !cs.perf_text.empty()) {
      const double ui = height / 600.0;
      constexpr int value_size = 12;
      constexpr double label_scale = 10.0 / value_size;
      constexpr unsigned outline_strength = 3;
      constexpr double column_gap = 6, row_gap = 1;
      const double label_y = cs.fonts.baseline(value_size, true) * (1 - label_scale);
      const auto rasterize_label = [&](const std::string& label_text) {
        // Render smaller labels directly from the native font at drawable density.
        cs.fonts.set_pixel_scale(ui * label_scale);
        auto label = cs.fonts.rasterize_glow(label_text, value_size, true, 0xd7dce2, outline_strength);
        cs.fonts.set_pixel_scale(ui);
        label.width *= label_scale;
        label.height *= label_scale;
        label.offset.x *= label_scale;
        label.offset.y *= label_scale;
        label.image.pixel_scale = ui;
        label.redraw.reset();
        return label;
      };
      struct PerfRow {
        realm::TextBitmap label, value;
        double height;
        bool regular;
      };
      std::vector<PerfRow> rows;
      double label_w = 0, value_w = 0, regular_w = 0, box_h = 0;
      std::istringstream in(cs.perf_text);
      for (std::string line; std::getline(in, line);) {
        const auto split = line.find(' ');
        if (split == std::string::npos) continue;
        const bool regular = line.ends_with(" FPS") || line.ends_with(" MB");
        auto label = regular ? realm::TextBitmap{} : rasterize_label(line.substr(0, split));
        auto value = cs.fonts.rasterize_glow(regular ? line : line.substr(split + 1), value_size, true, 0xffffff,
                                             outline_strength);
        const double row_h = regular ? value.height : std::max(label.height + label_y, value.height);
        if (regular)
          regular_w = std::max(regular_w, value.width);
        else {
          label_w = std::max(label_w, label.width);
          value_w = std::max(value_w, value.width);
        }
        box_h += row_h + row_gap;
        rows.push_back({std::move(label), std::move(value), row_h, regular});
      }
      // Light black outlines and smaller frame-time labels aligned to the value baseline.
      auto image = realm::ui_image(std::max(regular_w, label_w + column_gap + value_w) + 8, box_h + 8, ui);
      double row_y = 4;
      for (const auto& row : rows) {
        if (!row.regular)
          realm::ui_blit_text(image, row.label, 4 + label_w - row.label.width + row.label.offset.x,
                              row_y + label_y + row.label.offset.y);
        const double value_x = row.regular ? 4 : 4 + label_w + column_gap;
        realm::ui_blit_text(image, row.value, value_x + row.value.offset.x, row_y + row.value.offset.y);
        row_y += row.height + row_gap;
      }
      cs.perf_size = {static_cast<double>(image.width), static_cast<double>(image.height)};
      cs.perf_texture.update(image);
      cs.perf_dirty = false;
    }
    if (!cs.perf_text.empty()) {
      auto quad =
          cs.perf_texture.quad({4.0 * height / 600, 4.0 * height / 600}, {cs.perf_size.x, 0}, {0, cs.perf_size.y});
      overlay_storage[overlay.size()] = quad;
      overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    }
  }
  if (cs.hud && o.world && !menus && cs.hud_view.header.hover && !cs.options_screen.visible()) {
    // IconButton: TextToolTip(0x363636, 0x9B9B9B, title, "Hotkey: KEY", 200) at the pointer.
    const bool nexus = cs.hud_view.header.button == realm::HudState::Header::Button::nexus;
    const std::string title = nexus ? "Nexus" : "Options",
                      text = "Hotkey: " +
                             std::string(realm::key_name(cs.settings.get(nexus ? "escapeToNexus" : "options")));
    const auto key = title + "|" + text + "|" + std::to_string(height);
    if (key != cs.header_tip_key) {
      cs.header_tip = realm::render_text_tooltip(cs.fonts, title, text, 3552822, 10197915, 200);
      cs.header_tip_texture.update(cs.header_tip.image);
      cs.header_tip_key = key;
    }
    const double ui = height / 600.0;
    const auto pointer = cs.hud_input.pointer();
    const auto at =
        realm::text_tooltip_position({width + (pointer.x - 200) * ui, pointer.y * ui}, cs.header_tip.frame_width,
                                     cs.header_tip.frame_height, width, height, ui);
    const auto& image = cs.header_tip.image;
    auto quad = cs.header_tip_texture.quad({at.x - cs.header_tip.margin * ui, at.y - cs.header_tip.margin * ui},
                                           {static_cast<double>(image.width), 0},
                                           {0, static_cast<double>(image.height)}, 0, 0, image.width, image.height);
    overlay_storage[overlay.size()] = quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
  if (cs.dialog.visible() && !menus) {
    if (cs.dialog.update(cs.fonts, width, height)) cs.dialog_texture.update(cs.dialog.image());
    auto dim = realm::Quad{};
    dim.origin = {0, 0};
    dim.axis_x = {static_cast<float>(width), 0};
    dim.axis_y = {0, static_cast<float>(height)};
    dim.kind = 2;
    dim.multiply = {21 / 255.F, 21 / 255.F, 21 / 255.F, .6F};
    overlay_storage[overlay.size()] = dim;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    const auto& image = cs.dialog.image();
    auto frame_quad =
        cs.dialog_texture.quad(cs.dialog.canvas_position(), {image.width * cs.dialog.scale() / image.pixel_scale, 0},
                               {0, image.height * cs.dialog.scale() / image.pixel_scale});
    overlay_storage[overlay.size()] = frame_quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
}

}  // namespace realm::application
