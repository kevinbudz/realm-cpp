#pragma once
#include "realm/map/Camera.hpp"
#include "realm/assets/Image.hpp"
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace realm {
// GraphicsUtil.drawCutEdgeRect: corners flagged top-left, top-right,
// bottom-right, bottom-left are cut diagonally by `cut` pixels.
[[nodiscard]] std::vector<Vec2> cut_edge_rect(double x, double y, double width, double height, double cut,
                                              std::array<bool, 4> corners = {true, true, true, true});
// Antialiased even-odd fill and a centered, round-jointed stroke in logical
// coordinates, composited over a premultiplied surface.
void ui_path(Image& image, std::span<const Vec2> path, std::optional<std::uint32_t> fill,
             std::optional<std::uint32_t> stroke = {}, double stroke_width = 1, double fill_alpha = 1,
             double clip_right = 1e9);
// GuildChronicleScreen/Options backdrop over the whole window: 0x2B2B2B at alpha .8, MenuFrame's black
// glow band (alpha 0/.73/.73/0 at ratios 20/103/173/237 over y 508-588) and its 0x545454 bar at y 524-576.
// The image is logical_width x 600 design units and premultiplied.
void ui_menu_backdrop(Image& image, double logical_width);
}
