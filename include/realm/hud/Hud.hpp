#pragma once
#include "realm/ui/fonts/FontBook.hpp"
#include "realm/map/Map.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/objects/Party.hpp"
#include <array>
#include <optional>
#include <string_view>
namespace realm {
struct StatusBarState {
  std::string label;
  std::uint32_t color{};
  int y{}, value{}, maximum{}, boost{}, cap{-1};
  bool operator==(const StatusBarState&) const = default;
};
struct HudStatState {
  int value{}, boost{}, cap{};
  bool operator==(const HudStatState&) const = default;
};
struct HudRegion {
  int x{}, y{}, width{}, height{};
};
// A TextButton shape; centered buttons can start on a half pixel.
struct HudRect {
  double x{}, y{}, width{}, height{};
};
// flash.geom.ColorTransform without the alpha terms, which PartyPanel leaves at identity.
struct ColorTransform {
  std::array<double, 3> multiplier{1, 1, 1}, offset{};
};
struct HudState {
  std::string name;
  std::array<StatusBarState, 3> bars;
  struct Inventory {
    bool enabled{}, backpack{}, tiers{true};
    int selected{}, portrait{}, dragging{-1};
    int appearance_type{}, skin{};
    std::uint32_t tex1{}, tex2{};
    // Object ID of the nearby container whose ContainerGrid fills the
    // interaction panel; its eight slots follow the player's twenty.
    std::int32_t container{-1};
    std::array<int, 4> types{};
    std::array<int, 28> items = [] {
      std::array<int, 28> a;
      a.fill(-1);
      return a;
    }();
    std::array<bool, 4> dim{};
    std::array<bool, 28> restricted{};
    // Health/magic stack counts; -1 omits PotionInventoryView (meter-only fixtures).
    std::array<int, 2> potions{-1, -1};
    bool operator==(const Inventory&) const = default;
  } inventory;
  std::array<HudStatState, 6> stats{};
  // CharacterDetailsView's IconButton at (172, 4) below the minimap: Options in the Nexus, Nexus elsewhere.
  struct Header {
    enum class Button { none, options, nexus };
    Button button{Button::none};
    bool hover{};
    bool operator==(const Header&) const = default;
  } header;
  // PortalPanel for the nearest interactive Portal. The name already has
  // the "Locked " prefix removed for LockedPortal definitions.
  struct Portal {
    std::int32_t id{-1};
    std::string name;
    bool locked{}, active{true}, hover{};
    bool operator==(const Portal&) const = default;
  } portal;
  // ButtonPanel (or ReskinPanel) for the nearest object of a
  // button_panel_classes class; kind indexes those arrays.
  struct Button {
    int kind{-1};
    std::int32_t id{-1};
    bool hover{};
    bool operator==(const Button&) const = default;
  } button;
  // SellableObjectPanel for Merchant, ClosedVaultChest and GuildMerchant.
  // The panel is built when its object becomes the target: name, icon and a
  // shown rank notice stay as built (see retain); price, currency and the
  // button state follow every draw.
  struct Sellable {
    std::int32_t id{-1};
    std::string name, icon;
    int price{}, currency{-1};
    // Stars required by a shown "Rank Required:" notice, otherwise 0.
    int rank{};
    // "Leader Rank Required"; draw() never removes it once added.
    bool guild{}, button{}, enabled{true}, hover{};
    bool operator==(const Sellable&) const = default;
  } sellable;
  enum class SocialKind { none, hall, create_guild, leave_guild, choose_name, change_name };
  struct Social {
    SocialKind kind{SocialKind::none};
    std::int32_t id{-1};
    std::string title, notice, label;
    int price{}, currency{-1};
    bool enabled{}, hover{};
    bool operator==(const Social&) const = default;
  } social;
  // Carries the parts InteractPanel keeps while the target is unchanged.
  void retain(const HudState& previous);
  // The clickable button of the current interaction panel, if any. The
  // LegacyBuyButton width depends on the price text.
  struct PanelButton {
    std::int32_t id{-1};
    HudRect area{};
  };
  [[nodiscard]] PanelButton panel_button(const FontBook& fonts) const;
  // PartyPanel, shown while there is no interactive target. Each visible
  // GameObjectListItem has a portrait, a name and PartyPanel's ColorTransform.
  struct PartyMember {
    int type{};
    std::string name;
    std::uint32_t color{};
    // Not starred: MoreColorUtil.darkCT. Below 20% HP: the red pulse step
    // int(|sin(t/200)|*10), otherwise -1.
    bool dark{};
    int pulse{-1};
    int portrait{}, skin{};
    std::uint32_t tex1{}, tex2{};
    std::int32_t object_id{-1};
    bool operator==(const PartyMember&) const = default;
  };
  struct Party {
    bool shown{};
    std::array<std::optional<PartyMember>, 6> members;
    bool operator==(const Party&) const = default;
  } party;
  bool operator==(const HudState&) const = default;
  static HudState player(const World& world, const Entity& entity, const realm::Party* party = nullptr,
                         std::int32_t time_ms = 0);
  // Refresh the displayed identities without retaining Entity pointers across updates.
  void party_members(const World& world, const Entity& player, const realm::Party& party,
                     const std::array<std::int32_t, 6>& ids, std::int32_t time_ms);
};
// PotionInventoryView layout below the 84-pixel InventoryGrid.
inline constexpr int potion_view_y = 468;
inline constexpr std::array<int, 2> potion_view_x{14, 102};
inline constexpr std::array<int, 2> potion_types{2594, 2595};
// HUD slot indices at and above container_slot address the nearby container.
inline constexpr int container_slot = 20;
// The empty 172-pixel ContainerGrid is centered in the panel at HUD y 500.
inline constexpr int container_x = 14, container_y = 508;
// The IconButton's 40x40 bitmap, 12 units up and left of the button, in HUD coordinates.
inline constexpr HudRect header_button{160, 190, 40, 40};
// The TextButton "Enter" in HUD coordinates; drawCutEdgeRect cuts all four corners.
inline constexpr HudRect portal_button{73, 562, 54, 26};
// LegacyBuyButton("",16,...) layout in HUD coordinates.
struct BuyButtonLayout {
  HudRect area;
  double text_x{}, icon_x{};
  bool icon{};
};
[[nodiscard]] BuyButtonLayout buy_button_layout(const FontBook& fonts, const HudState::Sellable& state);
[[nodiscard]] std::optional<TextBitmap> tier_bitmap(const FontBook& fonts, const ObjectDef& item);
[[nodiscard]] Image rank_star_bitmap(const FontBook& fonts, const HudArt& art, int stars);
inline constexpr std::array<std::string_view, 4> button_panel_classes{"GuildBoard", "GuildChronicle",
                                                                      "CharacterChanger", "ReskinVendor"};
inline constexpr std::array<std::string_view, 4> button_panel_titles{"Guild Board", "Guild Chronicle",
                                                                     "Change Characters", "Change Skin"};
// TextButton(16,"View"/"View"/"Change"/"Choose") centered at HEIGHT - height - 4.
inline constexpr std::array<HudRect, 4> button_panel_buttons{
    {{74.5, 562, 51, 26}, {74.5, 562, 51, 26}, {65, 562, 70, 26}, {66, 562, 68, 26}}};
// Retain derived inventory/interaction state between packet changes. Hover and drag remain frame-driven.
class HudStateCache {
 public:
  const HudState& player(const World& world, const Entity& player, const Party* party, std::int32_t time);
  [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }

 private:
  HudState state_;
  const World* world_{};
  const Party* party_{};
  std::uint64_t generation_{}, revision_{}, party_revision_{}, rebuilds_{};
  std::int32_t player_id_{-1}, target_id_{-1};
  std::int64_t refreshed_{std::numeric_limits<std::int32_t>::min()};
  bool tiers_{};
};

// Persistent CPU panel. It is rebuilt only when visible character stats change.
// Finite original icon/decorative art is optional for meter-only fixtures.
class Hud {
 public:
  explicit Hud(const FontBook& fonts, const HudArt* art = nullptr, const Catalog* catalog = nullptr);
  bool update(const HudState& state);
  [[nodiscard]] const Image& image() const { return image_; }
  [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }
  [[nodiscard]] const std::vector<HudRegion>& changes() const { return changes_; }

 private:
  void blit(const Image& image, int x, int y, HudRegion clip = {0, 0, 200, 600});
  void inventory(const HudState::Inventory& state);
  void potions(const std::array<int, 2>& counts);
  void container(const HudState::Inventory& state);
  void portal(const HudState::Portal& state);
  void button(const HudState::Button& state);
  void sellable(const HudState::Sellable& state);
  void party(const HudState::Party& state);
  void social(const HudState::Social& state);
  void native_button(HudRect area, std::string_view label, bool hover);
  void tier(const ObjectDef& item, int x, int y);
  void draw(TextBitmap bitmap, double x, double y, double alpha = 1, const ColorTransform* transform = nullptr);
  // An unfiltered TextField renders straight into the stage rather than
  // through a filter bitmap, interpolating toward its color by coverage.
  void draw_direct(const TextBitmap& bitmap, double x, double y, std::uint32_t color);
  void tile(const HudState::Inventory& state, int index, int position, int x, int y, bool equipped);
  void stats(const std::array<HudStatState, 6>& state);
  void heading(const HudState& state);
  void rectangle(double x, int y, double width, int height, std::uint32_t color);
  void text(std::string_view value, int size, bool bold, std::uint32_t color, double x, double y, double alpha = 1,
            unsigned shadow_strength = 1, int field_width = 0);
  void bar(const StatusBarState& state);
  const FontBook& fonts_;
  const HudArt* art_{};
  const Catalog* state_catalog_{};
  Image image_{200, 600, std::vector<unsigned char>(200 * 600 * 4)};
  HudState state_;
  bool initialized_{};
  std::uint64_t rebuilds_{};
  std::vector<HudRegion> changes_;
};
}
