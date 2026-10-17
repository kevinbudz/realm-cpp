#pragma once
// Flash: kabam/rotmg/minimap/view/MiniMap.as
#include "realm/assets/AnimationLibrary.hpp"
#include "realm/map/World.hpp"
#include <unordered_map>

namespace realm {
// Logical side of the minimap view; its centre is minimap_half. The view is a square with its four
// corners cut by minimap_clip_cut (a triangle each, in logical px), matching the sidebar frames.
inline constexpr int minimap_size = 182;
inline constexpr double minimap_half = minimap_size / 2.0;
inline constexpr int minimap_clip_cut = 4;
// Marker offsets from the centre are limited so a 4 px dot stays inside the square and clear of the
// corner triangles: |x| and |y| <= half - 2, and |x| + |y| <= 2 half - 4 - cut.
inline constexpr double minimap_marker_limit = minimap_half - 2;
inline constexpr double minimap_marker_diagonal = 2 * minimap_half - 4 - minimap_clip_cut;

struct MinimapMarker {
  Vec2 position;
  std::uint32_t color{};

  bool operator==(const MinimapMarker& other) const {
    return position.x == other.position.x && position.y == other.position.y && color == other.color;
  }
};

struct MinimapView {
  Vec2 focus;
  double angle{};
  std::vector<MinimapMarker> markers;
};

// The map is remembered at one pixel per tile. Only changed source rows are
// recolored; an unchanged view does no rasterization or texture upload.
// Clamps a marker offset from the view centre (logical px) to the marker limits above.
[[nodiscard]] Vec2 clamp_marker_offset(Vec2 offset);

class Minimap {
 public:
  Minimap(const Catalog& catalog, const AnimationLibrary& animations,
          const std::filesystem::path& root);
  Minimap(const Catalog& catalog, const AnimationLibrary& animations,
          std::shared_ptr<const FlashImages> pngs);
  bool update(const World& world, const Entity& focus, double angle);
  bool render(const World& world, const MinimapView& view);
  // GPU path gathers markers and synchronizes fog memory without rasterizing the minimap_size view.
  const MinimapView& prepare_gpu(const World& world, const Entity& focus, double angle);

  [[nodiscard]] const Image& map_image() const { return map_; }

  [[nodiscard]] const Image& arrow_image() const { return arrow_; }

  [[nodiscard]] const Image& out_button_image() const { return out_button_; }

  [[nodiscard]] const Image& in_button_image() const { return in_button_; }

  [[nodiscard]] std::span<const int> changed_rows() const { return changed_rows_; }

  [[nodiscard]] int button_zoom() const { return button_zoom_; }

  void set_pixel_scale(double scale);
  // Logical minimap_size x minimap_size coordinates; strict radius and current entity order.
  [[nodiscard]] std::vector<std::int32_t> hover_players(const World& world, const Entity& focus,
                                                        Vec2 pointer) const;
  [[nodiscard]] Vec2 marker_position(const World& world, const Entity& focus, Vec2 position) const;
  void zoom_in();
  void zoom_out();

  [[nodiscard]] int zoom_index() const { return zoom_index_; }

  [[nodiscard]] const std::vector<double>& zoom_levels() const { return zoom_levels_; }

  [[nodiscard]] const Image& image() const { return image_; }

  [[nodiscard]] std::uint32_t ground_color(std::uint16_t type);
  [[nodiscard]] std::uint32_t object_color(std::uint16_t type, std::uint32_t id = 0);

  [[nodiscard]] std::uint64_t colored_rows() const { return colored_rows_; }

  [[nodiscard]] std::uint64_t redraws() const { return redraws_; }

 private:
  const MinimapView& gather(const World& world, const Entity& focus, double angle);
  std::vector<int> changed_rows_;
  bool sync(const World& world);
  [[nodiscard]] std::uint32_t texture_color(const TextureDef& texture, std::uint32_t id) const;
  void rectangle(double x, double y, double width, double height, std::uint32_t color);
  void bitmap(const Image& image, double x, double y, double sx, double sy, double multiplier = 1);
  const Catalog& catalog_;
  const AnimationLibrary& animations_;
  std::shared_ptr<const FlashImages> pngs_;
  Image arrow_;
  Image out_button_;
  Image in_button_;
  Image map_;
  Image image_{minimap_size, minimap_size,
               std::vector<unsigned char>(static_cast<std::size_t>(minimap_size) * minimap_size * 4)};
  std::unordered_map<std::uint16_t, std::uint32_t> ground_colors_;
  std::unordered_map<std::uint16_t, std::uint32_t> object_colors_;
  mutable std::unordered_map<std::uint64_t, std::uint32_t> texture_colors_;
  std::vector<std::uint64_t> rows_;
  std::vector<double> zoom_levels_;
  MinimapView view_;
  MinimapView pending_;
  int zoom_index_{};
  int drawn_zoom_{-1};
  int button_zoom_{-1};
  int drawn_button_{-2};
  std::uint64_t revision_{};
  std::uint64_t colored_rows_{};
  std::uint64_t redraws_{};
};
}
