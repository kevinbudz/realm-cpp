#include "ApplicationLoop.hpp"
#include "realm/util/Telemetry.hpp"
#include "realm/assets/FontBook.hpp"
#include "realm/hud/Hud.hpp"
#include "realm/ui/Trade.hpp"
#include "realm/ui/dialogs/InteractionDialog.hpp"
#include "realm/ui/tooltip/TextToolTip.hpp"
#include "realm/parameters/Settings.hpp"
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
    int dw{};
    int dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.dialog.update(cs.fonts, dw, dh)) {
        cs.dialog_texture.update(cs.dialog.image());
      }
    }
  }

  if (cs.members.visible()) {
    int dw{};
    int dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.members.update(cs.fonts, dw, dh, dt)) {
        cs.members_texture.update(cs.members.image());
      }
    }
  }

  if (cs.skins && cs.skins->visible()) {
    int dw{};
    int dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.skins->update(dw, dh, time)) {
        cs.skin_texture.update(cs.skins->image());
      }
    }
  }

  if (cs.board.visible()) {
    int dw{};
    int dh{};
    SDL_GL_GetDrawableSize(cs.session.window, &dw, &dh);
    if (dh > 0) {
      cs.fonts.set_pixel_scale(dh / 600.0);
      if (cs.board.update(cs.fonts, dw, dh, dt)) {
        cs.board_texture.update(cs.board.image());
      }
    }
  }
}

void build_modal_overlays(ClientState& cs, const Options& options, Frame& f) {
  auto& time = f.time;
  auto& menus = f.menus;
  auto& width = f.width;
  auto& height = f.height;
  auto& overlay_storage = f.overlay_storage;
  auto& overlay = f.overlay;

  if (cs.interaction.visible() && !menus) {
    cs.fonts.set_pixel_scale(realm::InteractionDialog::scale(width, height));
    if (cs.interaction.render(cs.fonts)) {
      cs.interaction_texture.update(cs.interaction.image());
    }
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
    if (cs.board.update(cs.fonts, width, height)) {
      cs.board_texture.update(cs.board.image());
    }
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
    auto frame_quad = cs.board_texture.quad(
        cs.board.canvas_position(), {image.width * cs.board.scale() / image.pixel_scale, 0},
        {0, image.height * cs.board.scale() / image.pixel_scale});
    overlay_storage[overlay.size()] = frame_quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }

  if (cs.members.visible() && !menus) {
    if (cs.members.update(cs.fonts, width, height)) {
      cs.members_texture.update(cs.members.image());
    }
    auto quad = cs.members_texture.quad({0, 0}, {static_cast<double>(width), 0},
                                        {0, static_cast<double>(height)});
    overlay_storage[overlay.size()] = quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }

  if (cs.skins && cs.skins->visible() && !menus) {
    if (cs.skins->update(width, height, time)) {
      const auto region = cs.skins->change();
      cs.skin_texture.update_region(cs.skins->image(), region.x, region.y, region.width,
                                    region.height);
    }
    auto quad = cs.skin_texture.quad({0, 0}, {static_cast<double>(width), 0},
                                     {0, static_cast<double>(height)});
    overlay_storage[overlay.size()] = quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }

  if (cs.options_screen) {
    cs.options_screen->update(!menus);
  }

  if (options.world && !menus) {
    if (const auto quad = cs.perf_quad(height)) {
      overlay_storage[overlay.size()] = *quad;
      overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    }
  }

  if (cs.hud && options.world && !menus && cs.hud_view.header.hover &&
      !cs.options_menu.visible()) {
    // IconButton: TextToolTip(0x363636, 0x9B9B9B, title, "Hotkey: KEY", 200) at the pointer.
    const bool nexus = cs.hud_view.header.button == realm::HudState::Header::Button::nexus;
    const std::string title = nexus ? "Nexus" : "Options";
    const std::string text =
        "Hotkey: " +
        std::string(realm::key_name(cs.settings.get(nexus ? "escapeToNexus" : "options")));
    const auto key = title + "|" + text + "|" + std::to_string(height);
    if (key != cs.header_tip_key) {
      cs.header_tip = realm::render_text_tooltip(cs.fonts, title, text, 3552822, 10197915, 200);
      cs.header_tip_texture.update(cs.header_tip.image);
      cs.header_tip_key = key;
    }
    const double scale = height / 600.0;
    const auto pointer = cs.hud_input.pointer();
    const auto at = realm::text_tooltip_position(
        {width + (pointer.x - 200) * scale, pointer.y * scale}, cs.header_tip.frame_width,
        cs.header_tip.frame_height, width, height, scale);
    const auto& image = cs.header_tip.image;
    auto quad = cs.header_tip_texture.quad(
        {at.x - cs.header_tip.margin * scale, at.y - cs.header_tip.margin * scale},
        {static_cast<double>(image.width), 0}, {0, static_cast<double>(image.height)}, 0, 0,
        image.width, image.height);
    overlay_storage[overlay.size()] = quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }

  if (cs.dialog.visible() && !menus) {
    if (cs.dialog.update(cs.fonts, width, height)) {
      cs.dialog_texture.update(cs.dialog.image());
    }
    auto dim = realm::Quad{};
    dim.origin = {0, 0};
    dim.axis_x = {static_cast<float>(width), 0};
    dim.axis_y = {0, static_cast<float>(height)};
    dim.kind = 2;
    dim.multiply = {21 / 255.F, 21 / 255.F, 21 / 255.F, .6F};
    overlay_storage[overlay.size()] = dim;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    const auto& image = cs.dialog.image();
    auto frame_quad = cs.dialog_texture.quad(
        cs.dialog.canvas_position(), {image.width * cs.dialog.scale() / image.pixel_scale, 0},
        {0, image.height * cs.dialog.scale() / image.pixel_scale});
    overlay_storage[overlay.size()] = frame_quad;
    overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
  }
}

}  // namespace realm::application
