#pragma once
// Flash: com/company/assembleegameclient/ui/guild/GuildPlayerList.as
#include "realm/appengine/SavedCharactersList.hpp"
#include "realm/ui/dialogs/Dialog.hpp"
#include "realm/net/Http.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/ui/Scrollbar.hpp"
#include <future>
#include <map>
#include <tuple>

namespace realm {
class GuildMembers {
 public:
  enum class State { closed, loading, list, confirm, pending, error };
  enum class Action { promote, demote, remove };

  struct Request {
    Action action;
    std::string name;
    int rank;
  };

  // Icons come from the HUD art: guild fame, the guild ranks and the remove cross.
  explicit GuildMembers(const HudArt& art);
  void open(std::string host, std::uint16_t port, net::FormFields credentials, std::string guild,
            std::string own, int rank);
  void close();
  void show(GuildMemberList list, std::string own, int rank);  // offline fixtures
  void permissions(std::string_view guild, int rank);
  void poll(double dt = 0);
  void receive(const net::incoming::Text& text, std::int32_t own_id);

  [[nodiscard]] std::optional<Request> take_request() {
    return std::exchange(outgoing_, std::nullopt);
  }

  [[nodiscard]] static std::string rank_name(int rank);
  [[nodiscard]] static int promoted(int rank);
  [[nodiscard]] static int demoted(int rank);
  [[nodiscard]] static bool allowed(int actor, int target, Action action);

  [[nodiscard]] bool visible() const { return state_ != State::closed; }

  [[nodiscard]] State state() const { return state_; }

  [[nodiscard]] const GuildMemberList& list() const { return list_; }

  [[nodiscard]] const Dialog& dialog() const { return dialog_; }

  [[nodiscard]] const Image& image() const { return image_; }

  [[nodiscard]] Vec2 position() const { return position_; }

  [[nodiscard]] double scale() const { return scale_; }

  [[nodiscard]] double document_height() const { return total_height_; }

  [[nodiscard]] double scroll() const { return scrollbar_.pos(); }

  bool update(const FontBook& fonts, double width, double height, double dt = 0);
  void pointer_move(Vec2 p);
  void pointer_down(Vec2 p);
  void pointer_up(Vec2 p);
  void wheel(double steps);
  void focus_lost();

 private:
  void load();
  void fail(std::string text, bool load_error);
  int hit(Vec2 p) const;

  Vec2 local(Vec2 p) const { return {(p.x - position_.x) / scale_, p.y / scale_}; }

  void confirm(int index);
  State state_{State::closed};
  GuildMemberList list_;
  std::string host_;
  std::string guild_;
  std::string own_;
  net::FormFields credentials_;
  std::uint16_t port_{};
  int rank_{};
  std::future<std::string> worker_;
  std::uint64_t generation_{};
  std::uint64_t worker_generation_{};
  bool queued_{};
  bool dirty_{true};
  bool load_error_{};
  bool has_scroll_{};
  int hover_{-1};
  int pressed_{-1};
  double stage_width_{};
  double stage_height_{};
  double scale_{1};
  double total_height_{};
  double pending_seconds_{};
  Vec2 position_{};
  HudRect continue_button_{};
  std::optional<Request> outgoing_;
  std::optional<Request> pending_;
  std::string expected_;
  const HudArt& art_;
  std::array<Image, 7> icons_{};
  Scrollbar scrollbar_{16, 400};
  Dialog dialog_;
  Image image_;
  // Text bitmaps by text, size, bold and color, holding only the text the last update drew.
  std::map<std::tuple<std::string, int, bool, std::uint32_t>, TextBitmap> texts_;
  double texts_scale_{1};
};
}
