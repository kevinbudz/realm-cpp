#pragma once
#include "realm/hud_input.hpp"
#include "realm/inventory.hpp"

namespace realm {
enum class TooltipOwner { player, container, merchant, potion };
struct ItemTooltipTarget {
    InventoryItem item, equipped;
    TooltipOwner owner{};
    int slot{-1}, player_type{};
    bool usable{}, backpack{};
    std::array<int,8> stats{};
    bool operator==(const ItemTooltipTarget&) const = default;
};
struct TooltipLine { std::string text;std::uint32_t color{0xb3b3b3};bool bold{}; };
struct ItemTooltipContent {
    std::string title;
    std::uint32_t title_color{0xffffff};
    std::vector<TooltipLine> lines;
};
[[nodiscard]] std::optional<ItemTooltipTarget> item_tooltip_target(const World& world,const HudState& hud,Vec2 pointer);
[[nodiscard]] ItemTooltipContent item_tooltip_content(const Catalog& catalog,const ItemTooltipTarget& target);
// One retained bitmap, uploaded only for content changes or scrolling.
// Pointer movement changes the overlay position without rasterization.
class ItemTooltip {
public:
    static constexpr int width=256,maximum_height=580;
    ItemTooltip(const Catalog& catalog,const FontBook& fonts,const HudArt& art):catalog_(catalog),fonts_(fonts),art_(art) {}
    bool update(const std::optional<ItemTooltipTarget>& target);
    bool scroll(int wheel_steps);
    [[nodiscard]] bool visible() const { return target_.has_value(); }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] int content_height() const { return content_.height; }
    [[nodiscard]] const Image& image() const { return image_; }
    [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }
    [[nodiscard]] static Vec2 position(Vec2 pointer,int window_width,int window_height,int height,double scale);
private:
    void render(const ItemTooltipTarget& target);
    void viewport();
    const Catalog& catalog_;
    const FontBook& fonts_;
    const HudArt& art_;
    std::optional<ItemTooltipTarget> target_;
    Image content_,image_{width,maximum_height,std::vector<unsigned char>(width*maximum_height*4)};
    int height_{},scroll_{};
    std::uint64_t rebuilds_{};
};
}
