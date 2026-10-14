#include "realm/hud/KeysView.hpp"
#include "flash_assets.hpp"
#include "realm/assets/Image.hpp"
#include <array>
#include <filesystem>

namespace realm {
namespace {
const std::array<Image, 5>& key_art() {
  static const std::array<Image, 5> images = [] {
    const auto root = std::filesystem::path(flash::source_root) / "src/kabam/rotmg/ui/view";
    return std::array<Image, 5>{load_png(root / "KeysView_keyBackgroundPng.png"),
                                load_png(root / "KeysView_purpleKeyPng.png"),
                                load_png(root / "KeysView_greenKeyPng.png"), load_png(root / "KeysView_redKeyPng.png"),
                                load_png(root / "KeysView_yellowKeyPng.png")};
  }();
  return images;
}
}  // namespace

std::optional<DavyKey> KeysView::key_named(std::string_view text) {
  if (text == "purple") return DavyKey::purple;
  if (text == "green") return DavyKey::green;
  if (text == "red") return DavyKey::red;
  if (text == "yellow") return DavyKey::yellow;
  return {};
}

Image render_keys_view(const KeysView& view, double density) {
  const auto& art = key_art();
  Image image = ui_image(176, 36, density);
  ui_blit(image, art[0], 0, 0);
  for (std::size_t i = 0; i < 4; ++i)
    if (view.lit(static_cast<DavyKey>(i))) ui_blit(image, art[i + 1], 12 + 40 * static_cast<double>(i), 12);
  return image;
}
}  // namespace realm
