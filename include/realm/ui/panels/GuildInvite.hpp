#pragma once
// Flash: com/company/assembleegameclient/ui/panels/GuildInvitePanel.as
#include "realm/hud/Hud.hpp"
#include "realm/ui/fonts/FontBook.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace realm {
// GuildInvitePanel state. The panel takes the interaction panel's override slot, where a trade request also shows, so
// a new invitation replaces a shown trade request and the other way round.
class GuildInvite {
 public:
  // GuildInvitePanel's Timer.
  static constexpr std::int32_t request_time = 20000;
  // The override slot in HUD logical coordinates, the same area a trade request uses.
  static constexpr int override_top = 500, override_height = 100;
  struct Invitation {
    std::string name, guild;
    bool operator==(const Invitation&) const = default;
  };
  // GameServerConnection.onInvitedToGuild with showGuildInvitePopup on.
  void invited(std::string name, std::string guild, std::int32_t time) {
    invitation_ = Invitation{std::move(name), std::move(guild)};
    invited_time_ = time;
    ++revision_;
  }
  [[nodiscard]] const std::optional<Invitation>& invitation() const { return invitation_; }
  // Reject, Accept or the 20-second timer close the panel.
  void close() {
    if (invitation_) {
      invitation_.reset();
      ++revision_;
    }
  }
  void update(std::int32_t time) {
    if (invitation_ && time - invited_time_ >= request_time) close();
  }
  [[nodiscard]] std::uint64_t revision() const { return revision_; }

 private:
  std::optional<Invitation> invitation_;
  std::int32_t invited_time_{};
  std::uint64_t revision_{};
};

// Renders GuildInvitePanel over the interaction panel, in the same HUD area as TradeView's request panel.
class GuildInviteView {
 public:
  static constexpr int width = 200;
  explicit GuildInviteView(const FontBook& fonts) : fonts_(fonts) {}
  // Rebuilds when the invitation, the hovered button or the density changes. Returns true when the image changed.
  bool update(const GuildInvite& invite, Vec2 pointer);
  [[nodiscard]] const Image& image() const { return image_; }
  // Reject and Accept in HUD logical coordinates.
  struct Buttons {
    HudRect reject, accept;
  };
  [[nodiscard]] Buttons buttons() const;

 private:
  void render(const GuildInvite::Invitation& invite);
  const FontBook& fonts_;
  std::uint64_t revision_{~0ULL};
  int hover_{-1};
  double scale_{};
  Image image_;
};
}  // namespace realm
