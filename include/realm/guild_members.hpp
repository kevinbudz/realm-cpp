#pragma once
#include "realm/account.hpp"
#include "realm/dialog.hpp"
#include "realm/http.hpp"
#include "realm/hud_art.hpp"
#include "realm/scrollbar.hpp"
#include <future>
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
  void open(std::string host, std::uint16_t port, net::FormFields credentials, std::string guild, std::string own,
            int rank);
  void close();
  void show(GuildMemberList list, std::string own, int rank);  // offline fixtures
  void permissions(std::string_view guild, int rank);
  void poll(double dt = 0);
  void receive(const net::incoming::Text& text, std::int32_t own_id);
  [[nodiscard]] std::optional<Request> take_request() { return std::exchange(outgoing_, std::nullopt); }
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
  void confirm(int hit);
  State state_{State::closed};
  GuildMemberList list_;
  std::string host_, guild_, own_;
  net::FormFields credentials_;
  std::uint16_t port_{};
  int rank_{};
  std::future<std::string> worker_;
  std::uint64_t generation_{}, worker_generation_{};
  bool queued_{}, dirty_{true}, load_error_{}, has_scroll_{};
  int hover_{-1}, pressed_{-1};
  double stage_width_{}, stage_height_{}, scale_{1}, total_height_{}, pending_seconds_{};
  Vec2 position_{};
  HudRect continue_button_{};
  std::optional<Request> outgoing_, pending_;
  std::string expected_;
  const HudArt& art_;
  std::array<Image, 7> icons_{};
  Scrollbar scrollbar_{16, 400};
  Dialog dialog_;
  Image image_;
};
}
