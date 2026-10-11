#pragma once
#include "realm/account.hpp"
#include "realm/font.hpp"
#include "realm/hud.hpp"
#include "realm/hud_art.hpp"
#include "realm/http.hpp"
#include "realm/renderer.hpp"
#include "realm/session.hpp"
#include <array>
#include <filesystem>
#include <future>
#include <optional>
#include <span>
#include <utility>
namespace realm {
struct StartupOptions {
    // Parameters.SERVER_ADDRESS, APP_PORT, GAME_PORT and BUILD_VERSION.
    std::string host{"127.0.0.1"},build_version{"1.0.0"};
    std::uint16_t app_port{7777},game_port{2050};
    std::filesystem::path account_file;
};
enum class StartupKey { backspace,tab,enter,escape };
// The menu half of the Flash client: StartupSequence (AccountLoadingScreen,
// LoadAccountTask, GetCharListTask), TitleView, CharacterSelectionAndNewsScreen,
// NewCharacterScreen and the web account dialogs. It ends by handing
// PlayGameCommand's GameSprite arguments to the caller as SessionOptions.
class Startup {
public:
    static constexpr int sheet_size=1024;
    Startup(const FontBook& fonts,const HudArt* art,const Catalog& catalog,const std::filesystem::path& flash_root,StartupOptions options);
    ~Startup();
    Startup(const Startup&)=delete;
    Startup& operator=(const Startup&)=delete;
    // Reserved atlas image holding every rasterized menu element.
    [[nodiscard]] const Image& sheet() const { return sheet_; }
    // MapBackground: the embedded map, written twice with a 10-tile border.
    void load_background(World& world);
    [[nodiscard]] bool menus() const { return screen_!=Screen::game; }
    [[nodiscard]] bool background() const { return menus()&&screen_!=Screen::account_loading; }
    [[nodiscard]] Vec2 background_position() const { return {background_x_,background_y_}; }
    static constexpr double background_angle=7*3.14159265358979323846/4;
    // Pointer coordinates are window pixels in the same space as width/height.
    void pointer_move(Vec2 position);
    void pointer_down(Vec2 position);
    void pointer_up(Vec2 position);
    void wheel(double steps);
    void text(std::string_view utf8);
    // Returns false when the menus do not use the key.
    bool key(StartupKey key);
    [[nodiscard]] bool wants_text() const { return dialog_==Dialog::login||dialog_==Dialog::reg; }
    void update(std::int32_t time_ms,double dt_ms,double width,double height);
    // Set once Play resolves to a character; the caller starts the session.
    [[nodiscard]] std::optional<SessionOptions> take_launch() { return std::exchange(launch_,std::nullopt); }
    void entered_game();
    void renamed_account(const std::string& name);
    // Session ended (death, failure, disconnect or leaving); returns to the
    // character screen with fresh data, showing message if it is not empty.
    void left_game(const std::string& message);
    [[nodiscard]] std::span<const HudRegion> uploads() const { return uploads_; }
    void clear_uploads() { uploads_.clear(); }
    // Window-space overlay quads for the size given to the last update().
    void build(const Atlas& atlas,std::size_t sheet_index,std::int32_t time_ms,std::vector<Quad>& out) const;
private:
    enum class Screen { account_loading,loading,title,characters,classes,entering,game };
    enum class Dialog { none,login,reg,message };
    enum class Task { none,verify_saved,char_list,login,reg };
    enum class Anchor { content,window };
    struct Element {
        HudRegion source{};
        // A hit element is the invisible input rectangle of its group.
        bool solid{},pulse{},full_width{},hit{};
        std::array<float,4> color{1,1,1,1},over{};
        Anchor anchor{Anchor::content};
        // Content: design coordinates. Window: the anchor is a fraction of the
        // window, the offset is scaled by the UI scale and pixels are unscaled.
        Vec2 position{},anchor_fraction{},pixels{},size{},pulse_center{};
        int group{-1};
    };
    struct Field { std::string label,value,error;bool password{}; };
    void rebuild();
    void reset_sheet();
    HudRegion store(const Image& image);
    Element& add_solid(double x,double y,double w,double h,std::uint32_t color,double alpha=1,int group=-1);
    // Adds a SimpleText; returns its TextField width and height.
    Vec2 add_text(std::string_view value,int size,bool bold,std::uint32_t color,double x,double y,int group=-1,bool shadow=true);
    Vec2 add_button(std::string_view label,int size,double x,double y,int action,bool pulse=false,bool center=false);
    void add_image(const Image& image,double x,double y,double scale=1,int group=-1);
    void add_window_text(std::string_view value,int size,bool bold,std::uint32_t color,Vec2 anchor,Vec2 offset,Vec2 pixels,bool align_right,bool align_bottom,int action=-1);
    void menu_frame(bool background);
    void account_header();
    void title_screen();
    void character_screen();
    void class_screen();
    void loading_screen(std::string_view text);
    void dialog_frame();
    [[nodiscard]] int hit(Vec2 position) const;
    [[nodiscard]] Vec2 to_window(const Element& element,Vec2 point) const;
    void activate(int action);
    void start_task(Task task,const std::string& path,net::FormFields fields);
    void finish_task(const std::string& response,const std::string& error);
    void with_valid_data(Screen next);
    void play(const SavedCharacter* character,int class_type);
    void open_dialog(Dialog dialog,std::string message={});
    void submit_dialog();
    [[nodiscard]] net::FormFields credentials() const;
    const FontBook& fonts_;
    const HudArt* art_;
    const Catalog& catalog_;
    std::filesystem::path root_;
    StartupOptions options_;
    SavedAccount saved_,account_;
    std::string account_save_error_;
    std::optional<CharList> list_;
    bool invalidated_{true};
    Screen screen_{Screen::account_loading},pending_screen_{Screen::title};
    Dialog dialog_{Dialog::none};
    std::string loading_text_{"Loading..."},dialog_message_,dialog_title_;
    std::vector<Field> fields_;
    std::size_t focus_{};
    Task task_{Task::none},retry_task_{Task::char_list};
    std::future<std::string> request_;
    std::int32_t retry_at_{-1},time_{};
    std::vector<int> player_types_;
    int new_character_type_{};
    double scroll_{},list_height_{};
    std::optional<SessionOptions> launch_;
    Image sheet_,logo_;
    int sheet_x_{1},sheet_y_{1},sheet_row_{},sheet_used_{};
    std::vector<HudRegion> uploads_;
    std::vector<Element> elements_;
    std::string background_map_;
    double background_x_{10},background_y_{10},background_width_{},width_{800},height_{600};
    int hovered_{-1},pressed_{-1};
    bool dirty_{true},caret_{};
};
}
