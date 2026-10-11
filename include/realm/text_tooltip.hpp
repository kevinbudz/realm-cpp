#pragma once
#include "realm/font.hpp"
#include <string_view>
namespace realm {
// ui/tooltip/TextToolTip: an optional bold 20 pixel title and 14 pixel text in a cut-edge frame with a
// one-pixel outline, wrapped to `max_width` and shadowed like every ToolTip (DropShadowFilter 16).
struct TextToolTipImage {
    Image image;
    // The sprite origin (the text origin) sits `margin` logical pixels inside the image.
    double margin{};
    // The frame's size in logical pixels: the content plus six on every side.
    double frame_width{},frame_height{};
};
[[nodiscard]] TextToolTipImage render_text_tooltip(const FontBook& fonts,std::string_view title,std::string_view text,
                                                   std::uint32_t background,std::uint32_t outline,int max_width);
// ToolTip.position() in stage pixels for the frame size in pixels: beside the pointer, flipped past the stage's
// half width or third of its height, and never closer than 12 pixels to the origin. Returns the frame's top-left.
[[nodiscard]] Vec2 text_tooltip_position(Vec2 pointer,double frame_width,double frame_height,double stage_width,double stage_height,double scale);
}
