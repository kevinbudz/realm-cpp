#include "realm/ui/panels/GuildInvite.hpp"
#include "realm/ui/GraphicsUtil.hpp"

namespace realm {
namespace {
constexpr std::uint32_t background = 3552822;
// The Panel's 188 by 84 content at (6, 508) in HUD coordinates, as TradeView's request panel places
// it.
constexpr double panel_x = 6;
constexpr double panel_top = 508;
constexpr double panel_height = 84;

bool inside(HudRect area, Vec2 p) {
  return p.x >= area.x && p.x < area.x + area.width && p.y >= area.y && p.y < area.y + area.height;
}

// TextButton(16, label) with its top edge at `top` in the panel image.
void text_button(const FontBook& fonts, Image& image, HudRect area, double top,
                 std::string_view label, bool hover) {
  const auto path = cut_edge_rect(area.x, area.y - top, area.width, area.height, 4);
  ui_path(image, path, hover ? 16768133U : 0xffffffU);
  const auto bitmap = fonts.rasterize(label, 16, true, false, 3552822);
  const double text_x = area.x + area.width / 2 - fonts.measure(label, 16, true) / 2 - 2;
  ui_blit_text(image, bitmap, text_x + bitmap.offset.x, area.y - top + 1 + bitmap.offset.y, false,
               true);
}
}  // namespace

GuildInviteView::Buttons GuildInviteView::buttons() const {
  // TextButton(16,label): the field width plus 12; the buttons sit at the bottom of the panel.
  const double reject = fonts_.layout("Reject", 16, true).width + 12;
  const double accept = fonts_.layout("Accept", 16, true).width + 12;
  const double y = panel_top + panel_height - 26 - 4;
  return {{panel_x + 47 - reject / 2, y, reject, 26}, {panel_x + 141 - accept / 2, y, accept, 26}};
}

bool GuildInviteView::update(const GuildInvite& invite, Vec2 pointer) {
  int hover = -1;
  if (invite.invitation()) {
    const auto area = buttons();
    if (inside(area.reject, pointer)) {
      hover = 0;
    } else if (inside(area.accept, pointer)) {
      hover = 1;
    }
  }
  if (invite.revision() == revision_ && hover == hover_ && fonts_.pixel_scale() == scale_) {
    return false;
  }
  revision_ = invite.revision();
  hover_ = hover;
  scale_ = fonts_.pixel_scale();
  if (invite.invitation()) {
    render(*invite.invitation());
  }
  return true;
}

void GuildInviteView::render(const GuildInvite::Invitation& invite) {
  const double top = GuildInvite::override_top;
  image_ = ui_image(width, GuildInvite::override_height, fonts_.pixel_scale());
  ui_rectangle(image_, 0, 0, width, GuildInvite::override_height, background, true);
  // SimpleText(16) title and guild name, bold and centered in the panel width, with a shadow. The
  // guild name sits 20 pixels below the title.
  const auto title =
      fonts_.rasterize_centered(invite.name + " invited you to:", 16, true, true, 0xffffff, 188);
  ui_blit_text(image_, title, panel_x + title.offset.x, panel_top - top + title.offset.y, false,
               true);
  const auto guild = fonts_.rasterize_centered(invite.guild, 16, true, true, 0xffffff, 188);
  ui_blit_text(image_, guild, panel_x + guild.offset.x, panel_top - top + 20 + guild.offset.y,
               false, true);
  const auto area = buttons();
  text_button(fonts_, image_, area.reject, top, "Reject", hover_ == 0);
  text_button(fonts_, image_, area.accept, top, "Accept", hover_ == 1);
}
}  // namespace realm
