#pragma once
#include "realm/dialog.hpp"
#include "realm/http.hpp"
#include "realm/scrollbar.hpp"
#include <future>
namespace realm {
class GuildBoard {
public:
    enum class State { closed,loading,view,edit,saving,error };
    enum class Key { left,right,up,down,home,end,backspace,del,enter,select_all };
    void open(std::string host,std::uint16_t port,net::FormFields credentials,int rank);
    void close();
    void permissions(int rank);
    // Also used by offline reference fixtures; production uses open/poll.
    void show(std::string text,int rank);
    void poll();
    [[nodiscard]] bool visible() const { return state_!=State::closed; }
    [[nodiscard]] bool wants_text() const { return state_==State::edit&&focused_; }
    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] const std::string& status_text() const { return dialog_.content().body; }
    [[nodiscard]] const std::string& text() const { return state_==State::edit||state_==State::saving||state_==State::error ? draft_ : saved_; }
    [[nodiscard]] double width() const { return width_; }
    [[nodiscard]] double height() const { return height_; }
    [[nodiscard]] double scale() const { return scale_; }
    [[nodiscard]] double scroll() const { return scrollbar_.pos(); }
    [[nodiscard]] Vec2 position() const { return position_; }
    [[nodiscard]] Vec2 canvas_position() const { return transient() ? dialog_.canvas_position() : Vec2{position_.x-8*scale_,position_.y-8*scale_}; }
    [[nodiscard]] bool backdrop() const { return !transient(); }
    [[nodiscard]] const Image& image() const { return transient() ? dialog_.image() : image_; }
    bool update(const FontBook& fonts,double width,double height,double dt=0);
    void pointer_move(Vec2 p);
    void pointer_down(Vec2 p,bool shift=false);
    void pointer_up(Vec2 p);
    void wheel(double steps);
    void focus_lost();
    void insert(std::string_view text);
    void key(Key key,bool shift=false,bool control=false);
    [[nodiscard]] std::string selection() const;
    void cut();
    [[nodiscard]] std::optional<std::string> take_link() { return std::exchange(link_,std::nullopt); }
    [[nodiscard]] HudRect button(int index) const { return {index==0 ? 4.0 : width_-124,404,120,height_-408}; }
private:
    struct Line { std::string text;std::size_t start{}; };
    struct Link { std::size_t start,end;HudRect rect; };
    struct UrlRange { std::size_t start,end; };
    bool transient() const { return state_==State::loading||state_==State::saving||state_==State::error; }
    Vec2 local(Vec2 p) const { return {(p.x-position_.x)/scale_,(p.y-position_.y)/scale_}; }
    int hit(Vec2 p) const;
    std::size_t caret_at(Vec2 p) const;
    void layout();
    void ensure_caret();
    [[nodiscard]] double text_offset() const;
    void edit();
    void start(bool save);
    State state_{State::closed};
    bool can_edit_{},focused_{},selecting_{},dirty_{true},layout_dirty_{true},retry_save_{},queued_{},request_save_{};
    int hover_{-1},pressed_{-1};
    std::string saved_,draft_,host_;
    std::uint16_t port_{};net::FormFields credentials_;
    std::future<std::string> request_;std::uint64_t generation_{},request_generation_{};
    std::size_t caret_{},anchor_{};
    double width_{400},height_{},scale_{1},stage_width_{},stage_height_{},line_height_{20},text_height_{};
    Vec2 position_{};const FontBook* fonts_{};
    std::vector<Line> lines_;std::vector<Link> links_;
    std::vector<UrlRange> urls_;
    Scrollbar scrollbar_;Dialog dialog_;Image image_;
    std::optional<std::string> link_;
    std::optional<std::pair<std::size_t,std::size_t>> pressed_link_;
};
}
