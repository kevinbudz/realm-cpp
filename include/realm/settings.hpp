#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace realm {
// Parameters.data_ and the Options screen's option table. Every value is an
// integer: key bindings hold Flash key codes (KeyCodes.as), toggles 1/0 and
// choices the value the Flash ChoiceOption lists. Option names are Flash's.
enum class OptionsTab { controls,gameplay,hotkeys,chat,graphics,sound };
inline constexpr std::size_t options_tab_count=6;
[[nodiscard]] std::string_view options_tab_name(OptionsTab tab);

struct OptionDef {
    enum class Kind { key,choice,spacer,hidden_key,hidden_value };
    OptionsTab tab{};
    Kind kind{Kind::key};
    std::string_view param,label,tooltip;
    int fallback{};
    // Choice labels and values in Flash's order; a click advances to the next.
    std::vector<std::pair<std::string_view,int>> choices;
    // KeyMapper disabled state: the rotate keys follow allowRotation.
    std::string_view enabled_by;
};
// Rows of every tab in Flash's add order, plus the parameters Flash registers
// without a row (escapeToNexus2, showTradePopup).
[[nodiscard]] std::span<const OptionDef> option_defs();
[[nodiscard]] std::vector<const OptionDef*> tab_options(OptionsTab tab);
[[nodiscard]] const OptionDef* find_option(std::string_view param);

// KeyCodes.CharCodeStrings; empty for a code without a name.
[[nodiscard]] std::string_view key_name(int code);
inline constexpr int key_unset=0,key_escape=27,key_enter=13;

// Settings the game systems read each frame.
struct Preferences {
    bool hp_bars{true},particles{true},draw_shadows{true},proj_outline{true},text_bubbles{true},
        ally_shots{true},ally_damage{true},ally_notifs{true},show_tier_tag{true},show_quest_portraits{true},
        show_guild_invite_popup{true},show_trade_popup{true},play_sfx{true},play_pew_pew{true};
};

class Settings {
public:
    Settings();
    [[nodiscard]] int get(std::string_view param) const;
    [[nodiscard]] bool flag(std::string_view param) const { return get(param)!=0; }
    // Parameters.setKey: any other registered binding on the same code is unset.
    void set_key(std::string_view param,int code);
    // ChoiceBox values only; anything else is ignored. Returns whether it changed.
    bool set_choice(std::string_view param,int value);
    // Options.onResetToDefaultsClick: only the rows of the visible tab are reset
    // (Flash deletes their values, then setDefaults fills the missing ones).
    void reset_tab(OptionsTab tab);
    void reset_all();
    // KeyMapper.setDisabled: whether the row currently accepts clicks.
    [[nodiscard]] bool enabled(const OptionDef& option) const { return option.enabled_by.empty()||flag(option.enabled_by); }
    // Parameters.cameraAngle: persisted with the settings.
    [[nodiscard]] double camera_angle() const { return camera_angle_; }
    void set_camera_angle(double angle) { camera_angle_=angle; }
    [[nodiscard]] double default_camera_angle() const;
    [[nodiscard]] Preferences preferences() const;
    // The key bound to an action, or key_unset.
    [[nodiscard]] int key(std::string_view param) const { return get(param); }
    [[nodiscard]] const std::map<std::string,int,std::less<>>& values() const { return values_; }

    // A missing, unreadable or malformed file yields the defaults. Unknown names are
    // dropped and invalid values fall back per option, so a hand-edited file cannot
    // inject a state the screen could not produce.
    [[nodiscard]] static Settings load(const std::filesystem::path& path);
    // Atomic write (temporary file and rename); false when the location is unwritable.
    bool save(const std::filesystem::path& path) const;
    [[nodiscard]] static std::filesystem::path path_beside(const std::filesystem::path& account_file);
private:
    [[nodiscard]] static bool valid(const OptionDef& option,int value);
    std::map<std::string,int,std::less<>> values_;
    double camera_angle_;
};
}
