#pragma once
#include "realm/map/World.hpp"
#include "realm/objects/Entity.hpp"
#include "realm/objects/Party.hpp"
#include "realm/assets/FontBook.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace realm {
struct StatusBarState {
  std::string label;
  std::uint32_t color{};
  int y{};
  int value{};
  int maximum{};
  int boost{};
  int cap{-1};
  // The value and maximum numbers are drawn (HP/MP Text, or the pointer over the bar).
  bool text{true};
  bool operator==(const StatusBarState&) const = default;
};

struct HudStatState {
  int value{};
  int boost{};
  int cap{};
  bool operator==(const HudStatState&) const = default;
};

struct HudRegion {
  int x{};
  int y{};
  int width{};
  int height{};
};

// A TextButton shape; centered buttons can start on a half pixel.
struct HudRect {
  double x{};
  double y{};
  double width{};
  double height{};
};

// flash.geom.ColorTransform without the alpha terms, which PartyPanel leaves at identity.
struct ColorTransform {
  std::array<double, 3> multiplier{1, 1, 1};
  std::array<double, 3> offset{};
};

struct HudState {
  std::string name;
  std::array<StatusBarState, 3> bars;

  struct Inventory {
    bool enabled{};
    bool backpack{};
    bool tiers{true};
    int selected{};
    int portrait{};
    int dragging{-1};
    int appearance_type{};
    int skin{};
    std::uint32_t tex1{};
    std::uint32_t tex2{};
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

  // CharacterDetailsView's IconButton at (172, 4) below the minimap: Options in the Nexus, Nexus
  // elsewhere.
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
    bool locked{};
    bool active{true};
    bool hover{};
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
    std::string name;
    std::string icon;
    int price{};
    int currency{-1};
    // Stars required by a shown "Rank Required:" notice, otherwise 0.
    int rank{};
    // "Leader Rank Required"; draw() never removes it once added.
    bool guild{};
    bool button{};
    bool enabled{true};
    bool hover{};
    bool operator==(const Sellable&) const = default;
  } sellable;
  enum class SocialKind { none, hall, create_guild, leave_guild, choose_name, change_name };

  struct Social {
    SocialKind kind{SocialKind::none};
    std::int32_t id{-1};
    std::string title;
    std::string notice;
    std::string label;
    int price{};
    int currency{-1};
    bool enabled{};
    bool hover{};
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
    int portrait{};
    int skin{};
    std::uint32_t tex1{};
    std::uint32_t tex2{};
    std::int32_t object_id{-1};
    bool operator==(const PartyMember&) const = default;
  };

  struct Party {
    bool shown{};
    std::array<std::optional<PartyMember>, 6> members;
    bool operator==(const Party&) const = default;
  } party;

  bool operator==(const HudState&) const = default;
  static HudState player(const World& world, const Entity& entity,
                         const realm::Party* party = nullptr, std::int32_t time_ms = 0);
  // Refresh the displayed identities without retaining Entity pointers across updates.
  void party_members(const World& world, const Entity& player, const realm::Party& party,
                     const std::array<std::int32_t, 6>& ids, std::int32_t time_ms);
};

// HUD slot indices at and above container_slot address the nearby container.
inline constexpr int container_slot = 20;
// Every TextButton in the side panel is at least as wide as the social "Renounce" button: its 16 px
// bold label plus 18 units. A narrower Flash field stays centred on HUD x 100.
inline constexpr double panel_button_min_width = 89;
constexpr HudRect text_button_area(double flash_width) {
  const double width = flash_width > panel_button_min_width ? flash_width : panel_button_min_width;
  return {100 - width / 2, 562, width, 26};
}
// The TextButton "Enter" in HUD coordinates; drawCutEdgeRect cuts all four corners.
inline constexpr HudRect portal_button = text_button_area(54);

// LegacyBuyButton("",16,...) layout in HUD coordinates.
struct BuyButtonLayout {
  HudRect area;
  double text_x{};
  double icon_x{};
  bool icon{};
};

[[nodiscard]] BuyButtonLayout buy_button_layout(const FontBook& fonts,
                                                const HudState::Sellable& state);

struct SocialButtonLayout {
  HudRect area;
  double text_x{};
};
// The label plus 18 units, plus 24 for a currency coin on the right, never less than
// panel_button_min_width. The label is centred in the button.
[[nodiscard]] SocialButtonLayout social_button_layout(const FontBook& fonts,
                                                      std::string_view label, int currency);

inline constexpr std::array<std::string_view, 4> button_panel_classes{
    "GuildBoard", "GuildChronicle", "CharacterChanger", "ReskinVendor"};
inline constexpr std::array<std::string_view, 4> button_panel_titles{
    "Guild Board", "Guild Chronicle", "Change Characters", "Change Skin"};
// TextButton(16,"View"/"View"/"Change"/"Choose") centered at HEIGHT - height - 4.
inline constexpr std::array<HudRect, 4> button_panel_buttons{{text_button_area(51),
                                                              text_button_area(51),
                                                              text_button_area(70),
                                                              text_button_area(68)}};

// Retain derived inventory/interaction state between packet changes. Hover and drag remain
// frame-driven.
class HudStateCache {
 public:
  const HudState& player(const World& world, const Entity& player, const Party* party,
                         std::int32_t time);

  [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }

 private:
  HudState state_;
  const World* world_{};
  const Party* party_{};
  std::uint64_t generation_{};
  std::uint64_t revision_{};
  std::uint64_t party_revision_{};
  std::uint64_t rebuilds_{};
  std::int32_t player_id_{-1};
  std::int32_t target_id_{-1};
  std::int64_t refreshed_{std::numeric_limits<std::int32_t>::min()};
  bool tiers_{};
};
}
