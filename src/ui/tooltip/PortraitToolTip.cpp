#include "realm/ui/tooltip/PortraitToolTip.hpp"
#include "realm/assets/AnimationLibrary.hpp"
#include "realm/ui/GraphicsUtil.hpp"

namespace realm {
bool PortraitToolTip::update(const QuestTarget& target) {
  if (target_ && *target_ == target && image_.pixel_scale == fonts_.pixel_scale()) {
    return false;
  }
  target_ = target;
  render();
  return true;
}

void PortraitToolTip::render() {
  const auto& target = *target_;
  const auto portrait =
      art_.game_object_portrait(target.type, target.skin, target.tex1, target.tex2);
  // BitmapUtil.cropToBitmapData(portrait, 10, 10, width - 20, height - 20); a portrait too small to
  // crop is shown whole.
  const bool croppable = portrait.width > 20 && portrait.height > 20;
  const auto cropped =
      croppable ? crop(portrait, 10, 10, portrait.width - 20, portrait.height - 20) : portrait;
  content_width_ = cropped.width;
  content_height_ = cropped.height;
  image_ = ui_image(canvas_width(), canvas_height(), fonts_.pixel_scale());
  ui_path(image_,
          cut_edge_rect(origin - 6, origin - 6, content_width_ + 12, content_height_ + 12, 4),
          0x5c1d1d, 0xfc8642, 1);
  ui_blit(image_, cropped, origin, origin, true, true);
  ++rebuilds_;
}
}
