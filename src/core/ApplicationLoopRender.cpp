#include "ApplicationLoop.hpp"
#include "realm/util/Telemetry.hpp"
#include "realm/messaging/GameServerConnection.hpp"
#include "realm/hud/Hud.hpp"
#include "realm/hud/HudInput.hpp"
#include "realm/ui/tooltip/EquipmentToolTip.hpp"
#include "realm/ui/tooltip/PlayerToolTip.hpp"
#include "realm/ui/menu/PlayerMenu.hpp"
#include "realm/ui/Chat.hpp"
#include "realm/ui/TradePanel.hpp"
#include "realm/ui/menu/PlayerGroup.hpp"
#include "realm/map/partyoverlay/PartyOverlay.hpp"
#include "realm/screens/Startup.hpp"
#include "realm/game/view/CreditDisplay.hpp"
#include "realm/game/view/SafeAreaDisplay.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ApplicationOptions.hpp"
#include "ApplicationState.hpp"

namespace realm::application {
void build_scene(ClientState& cs, const Options& o, Frame& f) {
  auto& dt = f.dt;
  auto& time = f.time;
  auto& width = f.width;
  auto& height = f.height;
  auto& clip_width = f.clip_width;
  cs.fonts.set_pixel_scale(height / 600.0);
  if (cs.startup) {
    REALM_ZONE("ui.startup");
    cs.startup->update(time, dt * 1000, width, height);
  }
  cs.quads.clear();
  clip_width = width;
  if (cs.startup && cs.startup->menus()) {
    if (cs.startup->background() && cs.background_world) {
      // MapBackground centers an at least 800x600 viewport on the stage.
      const double view_w = std::max(800, width), view_h = std::max(600, height);
      const auto at = cs.startup->background_position();
      cs.camera.configure(at, realm::Startup::background_angle, realm::Rect{-view_w / 2, -view_h / 2, view_w, view_h});
      cs.fonts.set_pixel_scale(1);
      cs.scene.build(cs.world, cs.camera, cs.atlas, cs.animations, nullptr, cs.terrain.get(), &cs.appearances, at,
                     realm::Startup::background_angle, 1, width, height, time, cs.quads);
      cs.fonts.set_pixel_scale(height / 600.0);
    }
  } else if (o.world) {
    if (width <= height / 3) throw std::runtime_error("Window too narrow for game camera");
    const auto shake = cs.world.particles().camera_offset(time);
    cs.camera.configure({cs.position.x + shake.x, cs.position.y + shake.y}, cs.angle, width, height, cs.zoom,
                        cs.centered);
    clip_width = static_cast<int>(cs.camera.playable_width());
    if (!o.map.empty() || cs.game_session || cs.stress) {
      // MAPINFO background (stars / nexus water), redone whenever a new map loads.
      if (cs.backdrop_generation != cs.world.generation()) {
        cs.backdrop.reset(cs.game_session ? cs.game_session->map_info().background : 0,
                          cs.game_session ? cs.game_session->map_info().seed : 1U);
        cs.backdrop_generation = cs.world.generation();
      }
      cs.backdrop.build(cs.camera, cs.atlas, time, cs.quads);
      cs.fonts.set_pixel_scale(cs.zoom);
      cs.scene.build(cs.world, cs.camera, cs.atlas, cs.animations, nullptr, cs.terrain.get(), &cs.appearances,
                     cs.position, cs.angle, cs.zoom, clip_width, height, o.scene_time >= 0 ? o.scene_time : time,
                     cs.quads);
      cs.fonts.set_pixel_scale(height / 600.0);
    }
  }
  if (cs.terrain)
    for (const auto slot : cs.terrain->changes())
      cs.atlas.update_additional(cs.terrain_first + slot, cs.terrain->images().at(slot));
}

void build_hud_overlays(ClientState& cs, const Options& o, Frame& f) {
  auto& time = f.time;
  auto& menus = f.menus;
  auto& width = f.width;
  auto& height = f.height;
  auto& ui_zone = f.ui_zone;
  auto& overlay_storage = f.overlay_storage;
  auto& overlay = f.overlay;
  ui_zone.emplace("ui.build");
  if (cs.startup) {
    // Session transitions may change the controller after its simulation update in this frame.
    cs.startup->update(time, 0, width, height);
    cs.startup_rml->update();
  }
  if (cs.hud && o.world && !menus)
    if (const auto* player = cs.world.entity(cs.world.player_id())) {
      const auto* party = cs.game_session ? &cs.game_session->party() : cs.stress ? cs.stress->party() : nullptr;
      auto state = cs.hud_state_cache.player(cs.world, *player, party, time);
      const bool hover_enabled = !cs.dialog.visible() && !cs.board.visible() && !cs.members.visible() &&
                                 !(cs.skins && cs.skins->visible()) && cs.tooltip_pointer_valid && !cs.hud_mouse_down &&
                                 !cs.interaction.visible() && !cs.trade.active() && !cs.trade.request() &&
                                 cs.game_session && cs.game_session->state() == realm::SessionState::playing;
      if (cs.game_session)
        cs.party_hover.update(cs.world, cs.game_session->party(), state, cs.hud_view, cs.hud_input.pointer(),
                              hover_enabled, time);
      else
        cs.party_hover.reset();
      if (cs.player_tooltip) {
        // PartyPanel.onMouseOver shows no tooltip while its menu is open.
        const auto target = hover_enabled && !(cs.player_menu && cs.player_menu->visible()) &&
                                    !(cs.player_group && cs.player_group->visible())
                                ? realm::player_tooltip_target(cs.world, cs.party_hover.target())
                                : std::nullopt;
        if (cs.player_tooltip->update(target)) cs.player_tooltip_texture.update(cs.player_tooltip->image());
      }
      if (!state.inventory.backpack && cs.hud_input.selected() == 2) cs.hud_input.cancel(true);
      if (state.inventory.container != cs.hud_container) {
        cs.hud_input.container_changed();
        cs.hud_container = state.inventory.container;
      }
      state.inventory.selected = cs.hud_input.selected();
      state.inventory.dragging = cs.hud_input.dragging();
      state.retain(cs.hud_view);
      state.sellable.enabled = !cs.game_session || !cs.game_session->buy_pending();
      // HUDModel.getButtonType: the Nexus has the Options button, every other map the Nexus button.
      state.header.button = cs.game_session && cs.game_session->game_id() != -1
                                ? realm::HudState::Header::Button::nexus
                                : realm::HudState::Header::Button::options;
      state.header.hover = !cs.trade.active() && cs.tooltip_pointer_valid && !cs.hud_mouse_down &&
                           !cs.options_screen.visible() && !cs.dialog.visible() && !cs.board.visible() &&
                           !cs.members.visible() && !(cs.skins && cs.skins->visible()) && !cs.interaction.visible() &&
                           realm::HudInput::button_at(realm::header_button, cs.hud_input.pointer());
      const auto panel_button = state.panel_button(cs.fonts);
      const bool hovered =
          panel_button.id >= 0 && realm::HudInput::button_at(panel_button.area, cs.hud_input.pointer());
      state.portal.hover = hovered && state.portal.id == panel_button.id;
      state.button.hover = hovered && state.button.id == panel_button.id;
      state.sellable.hover = hovered && state.sellable.id == panel_button.id;
      state.social.hover = hovered && state.social.id == panel_button.id;
      if (cs.hud->update(state))
        for (const auto& area : cs.hud->changes())
          cs.hud_texture.update_region(cs.hud->image(), area.x, area.y, area.width, area.height);
      cs.hud_view = state;
      if (cs.tooltip) {
        // EquipmentToolTip for trade slots replaces the hidden HUD grids.
        std::optional<realm::ItemTooltipTarget> target;
        if (cs.tooltip_pointer_valid && !cs.dialog.visible() && !cs.board.visible() && !cs.members.visible() &&
            !(cs.skins && cs.skins->visible()) && !cs.hud_mouse_down && !cs.interaction.visible()) {
          const auto pointer = cs.hud_input.pointer();
          if (cs.trade.active() && pointer.y >= realm::TradeView::panel_top) {
            int side = 0, slot = realm::TradeView::slot_at(pointer);
            if (slot < 0) {
              side = 1;
              slot = realm::TradeView::their_slot_at(pointer);
            }
            const auto& items = (side == 0 ? cs.trade.mine() : cs.trade.theirs()).items;
            if (slot >= 0 && items.at(static_cast<std::size_t>(slot)).item > 0)
              target = realm::item_tooltip_target(
                  cs.world, realm::InventoryItem{items.at(static_cast<std::size_t>(slot)).item, -1},
                  realm::TooltipOwner::other_player);
          } else if (!(cs.trade.request() && pointer.y >= realm::TradeView::request_top))
            target = realm::item_tooltip_target(cs.world, state, pointer);
        }
        if (target) target->use_special = cs.settings.get("useSpecial");
        cs.fonts.set_pixel_scale(std::min(height / 600.0, (width - 8.0) / realm::ItemTooltip::width));
        if (cs.tooltip->update(target)) cs.tooltip_texture.update(cs.tooltip->image());
        cs.fonts.set_pixel_scale(height / 600.0);
      }
      const double ui_scale = height / 600.0;
      overlay = {};
      // MapOverlay balloons sit beneath the HUD, chat and popups, in map pixels.
      cs.fonts.set_pixel_scale(cs.zoom);
      cs.balloons.update(cs.world, cs.fonts, time);
      cs.fonts.set_pixel_scale(height / 600.0);
      if (cs.settings.flag("textBubbles"))
        for (const auto& balloon : cs.balloons.balloons()) {
          const auto* speaker = cs.world.entity(balloon.object_id);
          if (!speaker) continue;
          auto& texture = cs.balloon_textures.at(balloon.slot);
          if (cs.balloon_uploaded.at(balloon.slot) != balloon.revision) {
            texture.update(balloon.image);
            cs.balloon_uploaded.at(balloon.slot) = balloon.revision;
          }
          const auto at = realm::SpeechBalloons::position(balloon, cs.camera.world_to_screen(speaker->motion.position),
                                                          speaker->appearance.size, cs.zoom);
          const double w = balloon.image.width / balloon.image.pixel_scale * cs.zoom,
                       h = balloon.image.height / balloon.image.pixel_scale * cs.zoom;
          overlay_storage[overlay.size()] =
              texture.quad(at, {w, 0}, {0, h}, 0, 0, balloon.image.width, balloon.image.height);
          overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
        }
      // CharacterStatusText: damage, conditions, notifications and level/exp/fame text float above the balloons.
      for (auto& pending : cs.world.take_status_events()) cs.status_texts.add(cs.world, std::move(pending));
      // Fixtures with a fixed effect clock age their text by --status-age after the first frame.
      const std::int32_t status_time = o.effect_time >= 0 ? o.effect_time + (cs.frame > 0 ? o.status_age : 0) : time;
      cs.fonts.set_pixel_scale(cs.zoom);
      cs.status_texts.update(cs.world, cs.status_art, status_time);
      cs.fonts.set_pixel_scale(height / 600.0);
      for (const auto& text : cs.status_texts.texts()) {
        if (!realm::StatusTexts::visible(text, status_time) || !text.label.image || overlay.size() >= 100) continue;
        const auto* speaker = cs.world.entity(text.object_id);
        if (!speaker) continue;
        const auto at = realm::StatusTexts::position(text, cs.camera.world_to_screen(speaker->motion.position),
                                                     speaker->appearance.size, cs.zoom, status_time);
        const double w = text.label.image->width / text.label.image->pixel_scale * cs.zoom,
                     h = text.label.image->height / text.label.image->pixel_scale * cs.zoom;
        const auto placed = cs.scene.status_quad(text.label.image, cs.atlas, at, {w, 0}, {0, h});
        std::shared_ptr<realm::SurfaceTexture> surface;
        if (!placed) surface = cs.scene.status_surface(text.label.image);
        auto quad = placed ? *placed : surface->quad(at, {w, 0}, {0, h});
        quad.owner = surface;
        overlay_storage[overlay.size()] = quad;
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
      if (cs.chat_view) {
        if (cs.chat_view->update(cs.chat, time) && !cs.chat_view->empty())
          cs.chat_texture.update(cs.chat_view->image());
        if (!cs.chat_view->empty()) {
          const auto& image = cs.chat_view->image();
          auto chat_quad =
              cs.chat_texture.quad({0, cs.chat_view->top() * ui_scale}, {realm::ChatView::width * ui_scale, 0},
                                   {0, cs.chat_view->surface_height() * ui_scale}, 0, 0, image.width, image.height);
          overlay_storage[overlay.size()] = chat_quad;
          overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
        }
      }
      auto q = cs.hud_texture.quad({width - 200 * ui_scale, 0}, {200 * ui_scale, 0}, {0, static_cast<double>(height)});
      q.kind = 5;
      overlay_storage[overlay.size()] = q;
      overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      if (cs.hud_art) {
        const int credits = player->profile.stats.value_or(realm::flash::StatId::CREDITS_STAT, 0),
                  fame = player->profile.stats.value_or(realm::flash::StatId::FAME_STAT, 0);
        if (credits != cs.displayed_credits || fame != cs.displayed_fame || cs.credit_density != ui_scale) {
          cs.credit_image = realm::render_credit_display(cs.fonts, *cs.hud_art, credits, fame);
          cs.credit_texture.update(cs.credit_image.image);
          cs.displayed_credits = credits;
          cs.displayed_fame = fame;
          cs.credit_density = ui_scale;
        }
        overlay_storage[overlay.size()] =
            cs.credit_texture.quad({width - 206 * ui_scale + cs.credit_image.origin_offset.x * ui_scale,
                                    cs.credit_image.origin_offset.y * ui_scale},
                                   {static_cast<double>(cs.credit_image.image.width), 0},
                                   {0, static_cast<double>(cs.credit_image.image.height)});
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
      if (cs.hud_art && cs.game_session && cs.game_session->map_info().show_displays) {
        const int stars = player->profile.stats.value_or(realm::flash::StatId::NUM_STARS_STAT, -1),
                  rank = player->profile.stats.value_or(realm::flash::StatId::GUILD_RANK_STAT, 0);
        if (stars != cs.displayed_stars || rank != cs.displayed_guild_rank ||
            player->profile.guild != cs.displayed_guild || cs.safe_area_density != ui_scale) {
          cs.safe_area_image =
              realm::render_safe_area_display(cs.fonts, *cs.hud_art, stars, player->profile.guild, rank);
          cs.safe_area_texture.update(cs.safe_area_image.image);
          cs.displayed_stars = stars;
          cs.displayed_guild_rank = rank;
          cs.displayed_guild = player->profile.guild;
          cs.safe_area_density = ui_scale;
        }
        overlay_storage[overlay.size()] = cs.safe_area_texture.quad(
            {cs.safe_area_image.origin_offset.x * ui_scale, cs.safe_area_image.origin_offset.y * ui_scale},
            {static_cast<double>(cs.safe_area_image.image.width), 0},
            {0, static_cast<double>(cs.safe_area_image.image.height)});
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
      if (cs.minimap) {
        overlay_storage[overlay.size()] = cs.minimap_gpu.build(*cs.minimap, cs.world, *player, cs.angle,
                                                               {width - 196 * ui_scale, 4 * ui_scale}, ui_scale);
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
      if (cs.trade_view) {
        cs.trade.update(time);
        if (cs.trade_view->update(cs.trade, cs.hud_input.pointer(), time)) {
          if (cs.trade.active())
            cs.trade_texture.update(cs.trade_view->panel());
          else if (cs.trade.request())
            cs.trade_request_texture.update(cs.trade_view->request());
        }
        if (cs.trade.active() || cs.trade.request()) {
          const bool panel = cs.trade.active();
          const auto& image = panel ? cs.trade_view->panel() : cs.trade_view->request();
          const double top = panel ? realm::TradeView::panel_top : realm::TradeView::request_top,
                       h = panel ? realm::TradeView::panel_height : realm::TradeView::request_height;
          auto trade_quad = (panel ? cs.trade_texture : cs.trade_request_texture)
                                .quad({width - 200 * ui_scale, top * ui_scale}, {200 * ui_scale, 0}, {0, h * ui_scale},
                                      0, 0, image.width, image.height);
          overlay_storage[overlay.size()] = trade_quad;
          overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
        }
      }
      if (cs.hud_art && cs.hud_input.dragging() >= 0) {
        const auto slot = static_cast<std::size_t>(cs.hud_input.dragging());
        const int type = state.inventory.items.at(slot);
        if (type > 0) {
          const auto key = (slot < 4 && state.inventory.dim.at(slot) ? "dim" : "item") + std::to_string(type);
          if (cs.drag_key != key) {
            const auto& icon = cs.hud_art->image(key);
            if (icon.width > 128 || icon.height > 128) throw std::runtime_error("Drag icon exceeds reserved image");
            std::fill(cs.drag_bitmap.pixels.begin(), cs.drag_bitmap.pixels.end(), 0);
            for (int y = 0; y < icon.height; ++y)
              for (int x = 0; x < icon.width; ++x) {
                const auto src =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(icon.width) + static_cast<std::size_t>(x)) *
                    4;
                const auto dst = (static_cast<std::size_t>(y + 64 - icon.height / 2) * 128 +
                                  static_cast<std::size_t>(x + 64 - icon.width / 2)) *
                                 4;
                const unsigned alpha = icon.pixels[src + 3];
                cs.drag_bitmap.pixels[dst + 3] = static_cast<unsigned char>(alpha);
                for (unsigned c = 0; c < 3; ++c)
                  cs.drag_bitmap.pixels[dst + c] =
                      static_cast<unsigned char>(alpha == 255 ? icon.pixels[src + c]
                                                 : alpha ? static_cast<unsigned>(icon.pixels[src + c]) * 256 / alpha
                                                         : 0);
              }
            cs.drag_surface.update(cs.drag_bitmap);
            cs.drag_key = key;
          }
          const auto pointer = cs.hud_input.pointer();
          auto drag = cs.drag_surface.quad({width + (pointer.x - 264) * ui_scale, (pointer.y - 64) * ui_scale},
                                           {128 * ui_scale, 0}, {0, 128 * ui_scale});
          drag.kind = 5;
          overlay_storage[overlay.size()] = drag;
          overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
        }
      }
      if (cs.tooltip && cs.tooltip->visible()) {
        const auto pointer = cs.hud_input.pointer();
        const realm::Vec2 screen_pointer{width + (pointer.x - 200) * ui_scale, pointer.y * ui_scale};
        const double scale = std::min(ui_scale, (width - 8.0) / realm::ItemTooltip::width);
        const auto at = realm::ItemTooltip::position(screen_pointer, width, height, cs.tooltip->height(), scale);
        auto tip = cs.tooltip_texture.quad(
            at, {realm::ItemTooltip::width * scale, 0}, {0, cs.tooltip->height() * scale}, 0, 0,
            cs.tooltip->image().width,
            static_cast<int>(std::ceil(cs.tooltip->height() * cs.tooltip->image().pixel_scale)));
        overlay_storage[overlay.size()] = tip;
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
      if (cs.player_tooltip && cs.player_tooltip->visible()) {
        const auto pointer = cs.hud_input.pointer();
        const realm::Vec2 screen_pointer{width + (pointer.x - 200) * ui_scale, pointer.y * ui_scale};
        const double scale = ui_scale;
        const auto at = realm::PlayerTooltip::position(screen_pointer, width, height, cs.player_tooltip->draw_width(),
                                                       cs.player_tooltip->height(), scale);
        const double density = cs.player_tooltip->image().pixel_scale;
        auto tip = cs.player_tooltip_texture.quad(
            at, {cs.player_tooltip->draw_width() * scale, 0}, {0, cs.player_tooltip->height() * scale}, 0, 0,
            static_cast<int>(std::ceil(cs.player_tooltip->draw_width() * density)),
            static_cast<int>(std::ceil(cs.player_tooltip->height() * density)));
        overlay_storage[overlay.size()] = tip;
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
    }
}

void build_group_overlays(ClientState& cs, const Options& o, Frame& f) {
  auto& time = f.time;
  auto& menus = f.menus;
  auto& width = f.width;
  auto& height = f.height;
  auto& overlay_storage = f.overlay_storage;
  auto& overlay = f.overlay;
  if (cs.player_group && cs.hud && cs.minimap && o.world && !menus) {
    const auto pointer = cs.hud_input.pointer();
    const double scale = height / 600.0;
    const realm::Vec2 screen_pointer{width + (pointer.x - 200) * scale, pointer.y * scale};
    const bool enabled = !cs.dialog.visible() && !cs.board.visible() && !cs.members.visible() &&
                         !(cs.skins && cs.skins->visible()) && !cs.interaction.visible() && cs.game_session &&
                         cs.game_session->state() == realm::SessionState::playing;
    if (!enabled) {
      cs.player_group->close();
      cs.party_arrows.reset();
    } else {
      cs.party_arrows.update(cs.world, cs.game_session->party(), cs.camera, screen_pointer, cs.tooltip_pointer_valid,
                             width, height);
      for (std::size_t i = 0; i < cs.party_arrows.arrows().size(); ++i) {
        const auto& arrow = cs.party_arrows.arrows()[i];
        if (arrow.players.empty()) continue;
        const double density = std::max(1.0, cs.camera.scale());
        if (cs.arrow_densities[i] != density || cs.arrow_rotations[i] != arrow.rotation ||
            cs.arrow_hovers[i] != arrow.hover) {
          cs.arrow_textures[i].update(realm::PartyArrows::image(arrow.rotation, arrow.hover, density));
          cs.arrow_densities[i] = density;
          cs.arrow_rotations[i] = arrow.rotation;
          cs.arrow_hovers[i] = arrow.hover;
        }
        const double a_scale = cs.camera.scale();
        const auto quad = cs.arrow_textures[i].quad({arrow.position.x - realm::PartyArrows::origin * a_scale,
                                                     arrow.position.y - realm::PartyArrows::origin * a_scale},
                                                    {realm::PartyArrows::canvas_size * a_scale, 0},
                                                    {0, realm::PartyArrows::canvas_size * a_scale});
        overlay_storage[overlay.size()] = quad;
        overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
      }
    }
    if (enabled && cs.player_group->menu()) {
      if (cs.player_group->refresh_appearance(cs.world)) cs.group_texture.update(cs.player_group->image());
      if (cs.player_group->update(screen_pointer, cs.tooltip_pointer_valid, cs.game_session->teleport_cooldown(time),
                                  scale))
        cs.group_texture.update(cs.player_group->image());
    } else if (enabled && cs.party_arrows.hovered() && !(cs.player_menu && cs.player_menu->visible())) {
      const auto& arrow = *cs.party_arrows.hovered();
      cs.fonts.set_pixel_scale(scale * cs.camera.scale());
      if (cs.player_group->arrow_tooltip(realm::player_group_targets(cs.world, arrow.players), arrow.position,
                                         arrow.rotation, cs.camera.scale(), scale * cs.camera.scale()))
        cs.group_texture.update(cs.player_group->image());
      cs.fonts.set_pixel_scale(scale);
    } else if (enabled)
      if (const auto* focus = cs.world.entity(cs.world.player_id())) {
        const auto ids = (!cs.tooltip_pointer_valid || (cs.player_menu && cs.player_menu->visible()))
                             ? std::vector<std::int32_t>{}
                             : cs.minimap->hover_players(cs.world, *focus, {pointer.x - 4, pointer.y - 4});
        if (cs.player_group->tooltip(realm::player_group_targets(cs.world, ids), screen_pointer, width, height, scale))
          cs.group_texture.update(cs.player_group->image());
      }
    if (cs.player_group->visible()) {
      const auto& image = cs.player_group->image();
      auto quad = cs.group_texture.quad(
          cs.player_group->canvas_position(), {cs.player_group->canvas_width() * cs.player_group->scale(), 0},
          {0, cs.player_group->canvas_height() * cs.player_group->scale()}, 0, 0, image.width, image.height);
      overlay_storage[overlay.size()] = quad;
      overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    }
  }
  if (cs.player_menu && cs.player_menu->visible() && cs.hud && o.world && !menus) {
    const auto pointer = cs.hud_input.pointer();
    const double ui_scale = height / 600.0;
    const realm::Vec2 screen_pointer{width + (pointer.x - 200) * ui_scale, pointer.y * ui_scale};
    const bool valid = cs.tooltip_pointer_valid && !cs.dialog.visible() && !cs.board.visible() &&
                       !cs.members.visible() && !(cs.skins && cs.skins->visible()) && !cs.interaction.visible();
    if (cs.player_menu->update(cs.world, screen_pointer, valid,
                               cs.game_session ? cs.game_session->teleport_cooldown(time) : 0, ui_scale))
      cs.player_menu_texture.update(cs.player_menu->image());
    if (cs.player_menu->visible()) {
      const auto& image = cs.player_menu->image();
      const double scale = cs.player_menu->scale();
      auto menu_quad =
          cs.player_menu_texture.quad(cs.player_menu->canvas_position(), {realm::PlayerMenu::canvas_width * scale, 0},
                                      {0, realm::PlayerMenu::canvas_height * scale}, 0, 0, image.width, image.height);
      overlay_storage[overlay.size()] = menu_quad;
      overlay = std::span<const realm::Quad>(overlay_storage).first(overlay.size() + 1);
    }
  }
}

}  // namespace realm::application
