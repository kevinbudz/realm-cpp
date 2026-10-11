#pragma once
#include "realm/animation.hpp"
#include "realm/world.hpp"
#include <unordered_map>
namespace realm {
struct MinimapMarker { Vec2 position;std::uint32_t color{};bool operator==(const MinimapMarker& other) const { return position.x==other.position.x&&position.y==other.position.y&&color==other.color; } };
struct MinimapView {
    Vec2 focus;
    double angle{};
    std::vector<MinimapMarker> markers;
};
// The map is remembered at one pixel per tile. Only changed source rows are
// recolored; an unchanged view does no rasterization or texture upload.
class Minimap {
public:
    Minimap(const Catalog& catalog,const AnimationLibrary& animations,const std::filesystem::path& root);
    bool update(const World& world,const Entity& focus,double angle);
    bool render(const World& world,const MinimapView& view);
    void zoom_in();
    void zoom_out();
    [[nodiscard]] int zoom_index() const { return zoom_index_; }
    [[nodiscard]] const std::vector<double>& zoom_levels() const { return zoom_levels_; }
    [[nodiscard]] const Image& image() const { return image_; }
    [[nodiscard]] std::uint32_t ground_color(std::uint16_t type);
    [[nodiscard]] std::uint32_t object_color(std::uint16_t type,std::uint32_t id=0);
    [[nodiscard]] std::uint64_t colored_rows() const { return colored_rows_; }
    [[nodiscard]] std::uint64_t redraws() const { return redraws_; }
private:
    bool sync(const World& world);
    [[nodiscard]] std::uint32_t texture_color(const TextureDef& texture,std::uint32_t id) const;
    void rectangle(double x,double y,double width,double height,std::uint32_t color);
    void bitmap(const Image& image,double x,double y,double sx,double sy,double multiplier=1);
    const Catalog& catalog_;
    const AnimationLibrary& animations_;
    std::vector<Image> assets_;
    Image arrow_,out_button_,in_button_,map_,image_{192,192,std::vector<unsigned char>(192*192*4)};
    std::unordered_map<std::uint16_t,std::uint32_t> ground_colors_,object_colors_;
    mutable std::unordered_map<std::uint64_t,std::uint32_t> texture_colors_;
    std::vector<std::uint64_t> rows_;
    std::vector<double> zoom_levels_;
    MinimapView view_,pending_;
    int zoom_index_{},drawn_zoom_{-1},button_zoom_{-1},drawn_button_{-2};
    std::uint64_t revision_{},colored_rows_{},redraws_{};
};
}
