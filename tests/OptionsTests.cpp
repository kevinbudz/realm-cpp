#include "realm/ui/options/Options.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
namespace {
int checks{};
void check(bool value, const char* text) {
  ++checks;
  if (!value) throw std::runtime_error(text);
}
}
using namespace realm;
namespace {
Vec2 center(const OptionsScreen& screen, HudRect r) {
  return {screen.position().x + (r.x + r.width / 2) * screen.scale(), (r.y + r.height / 2) * screen.scale()};
}
void click(OptionsScreen& screen, Vec2 p) {
  screen.pointer_move(p);
  screen.pointer_down(p);
  screen.pointer_up(p);
}
int find(const OptionsScreen& screen, std::string_view param) {
  for (std::size_t i = 0; i < screen.option_count(); ++i)
    if (screen.option(static_cast<int>(i)).param == param) return static_cast<int>(i);
  return -1;
}
void settings_rules(const std::filesystem::path& scratch) {
  Settings s;
  check(s.get("moveUp") == 87 && s.get("moveLeft") == 65 && s.get("moveDown") == 83 && s.get("moveRight") == 68,
        "Flash movement defaults");
  check(s.get("escapeToNexus") == 45 && s.get("escapeToNexus2") == 116 && s.get("options") == 79 &&
            s.get("interact") == 48 && s.get("chat") == 13,
        "Flash hot key defaults");
  check(s.get("togglePerformanceStats") == 0 && s.get("toggleCentering") == 0 && s.get("toggleFullscreenMode") == 0,
        "unset by default");
  check(!s.flag("allowRotation") && s.flag("hpBars") && s.flag("particles") && s.flag("allyNotifs") &&
            !s.flag("fullscreenMode"),
        "toggle defaults");
  check(s.get("maxFPS") == 60 && s.get("cursor") == 4 && s.flag("vsync") && s.flag("quality") && s.flag("playSFX") &&
            s.flag("showTradePopup"),
        "graphics defaults");
  check(std::fabs(s.camera_angle() - 7 * std::numbers::pi / 4) < 1e-12 && s.default_camera_angle() == s.camera_angle(),
        "45 degree default camera angle");
  // Parameters.setKey unsets any other binding of the same key, including the row-less escapeToNexus2.
  s.set_key("moveUp", 83);
  check(s.get("moveUp") == 83 && s.get("moveDown") == 0, "binding a taken key unsets its previous owner");
  s.set_key("useSpecial", 116);
  check(s.get("useSpecial") == 116 && s.get("escapeToNexus2") == 0, "hidden bindings take part in conflicts");
  s.set_key("togglePerformanceStats", 0);
  check(s.get("toggleCentering") == 0 && s.get("moveUp") == 83, "unsetting a key clears nothing else");
  s.set_key("maxFPS", 60);
  s.set_key("moveLeft", 999);
  s.set_key("nonsense", 65);
  check(s.get("maxFPS") == 60 && s.get("moveLeft") == 65, "non-key parameters and invalid codes are ignored");
  check(!s.set_choice("hpBars", 7) && s.flag("hpBars") && !s.set_choice("moveUp", 1) && !s.set_choice("hpBars", 1),
        "invalid or unchanged choices report no change");
  check(s.set_choice("hpBars", 0) && !s.flag("hpBars") && s.set_choice("cursor", -1) && s.get("cursor") == -1,
        "valid choices apply");
  check(s.set_choice("defaultCameraAngle", 0) && s.camera_angle() == 0 && s.default_camera_angle() == 0,
        "default camera angle resets the camera angle");
  // Only the visible tab is reset.
  s.reset_tab(OptionsTab::gameplay);
  check(s.flag("hpBars") && s.get("cursor") == 4 && s.get("moveUp") == 83 && s.get("defaultCameraAngle") == 1,
        "reset affects one tab");
  check(s.preferences().hp_bars && s.preferences().ally_notifs && s.preferences().show_trade_popup,
        "preferences follow values");
  s.set_choice("allyNotifs", 0);
  s.set_choice("particles", 0);
  s.set_choice("textBubbles", 0);
  const auto prefs = s.preferences();
  check(!prefs.ally_notifs && !prefs.particles && !prefs.text_bubbles && prefs.hp_bars,
        "preferences expose gated systems");
  // Persistence.
  const auto path = scratch / "settings.json";
  s.set_camera_angle(1.25);
  check(s.save(path) && !std::filesystem::exists(path.string() + ".tmp"), "atomic save leaves no temporary file");
  const auto loaded = Settings::load(path);
  check(loaded.values() == s.values() && loaded.camera_angle() == 1.25, "settings round trip");
  check(Settings::load(scratch / "missing.json").values() == Settings().values(), "missing file yields defaults");
  {
    std::ofstream bad(scratch / "bad.json");
    bad << "{ not json";
  }
  check(Settings::load(scratch / "bad.json").values() == Settings().values(), "corrupt file yields defaults");
  {
    std::ofstream odd(scratch / "odd.json");
    odd << R"({"values":{"moveUp":65,"hpBars":7,"maxFPS":144,"nobody":1,"cursor":"x","chat":-5,"particles":0},"cameraAngle":"NaN"})";
  }
  const auto odd = Settings::load(scratch / "odd.json");
  check(odd.get("moveUp") == 65 && odd.get("maxFPS") == 144 && !odd.flag("particles"), "valid stored values load");
  check(odd.flag("hpBars") && odd.get("cursor") == 4 && odd.get("chat") == 13 && !odd.values().contains("nobody"),
        "invalid stored values fall back per option");
  check(odd.camera_angle() == odd.default_camera_angle(), "missing camera angle falls back to the default");
  check(Settings::path_beside("/a/b/account.json") == "/a/b/account.settings.json",
        "settings sit beside an explicit account file");
  check(!s.save("/proc/forbidden/settings.json"), "unwritable location reports failure");
  // Option table shape (Options.as add order, spacers count as grid cells).
  check(tab_options(OptionsTab::controls).size() == 14 && tab_options(OptionsTab::gameplay).size() == 9 &&
            tab_options(OptionsTab::hotkeys).size() == 16,
        "tab sizes");
  check(tab_options(OptionsTab::chat).size() == 6 && tab_options(OptionsTab::graphics).size() == 9 &&
            tab_options(OptionsTab::sound).size() == 3,
        "tab sizes 2");
  std::set<std::string_view> seen;
  for (const auto& option : option_defs())
    if (!option.param.empty()) check(seen.insert(option.param).second, "unique parameter names");
  check(key_name(0) == "[Unset]" && key_name(45) == "Insert" && key_name(191) == "/" && key_name(220) == "\\" &&
            key_name(222) == "'" && key_name(33) == "PgUp" && key_name(1).empty(),
        "KeyCodes names");
}
void screen_behavior(const FontBook& fonts_in) {
  FontBook& fonts = const_cast<FontBook&>(fonts_in);
  Settings settings;
  OptionsScreen screen;
  check(!screen.visible() && !screen.update(fonts, 800, 600, 0), "closed screen draws nothing");
  screen.open(settings);
  fonts.set_pixel_scale(1);
  check(screen.visible() && screen.update(fonts, 800, 600, 0) && screen.tab() == OptionsTab::controls,
        "opens on Controls");
  check(screen.image().width == 800 && screen.image().height == 600 && screen.position().x == 0 && screen.scale() == 1,
        "design-space image");
  check(!screen.update(fonts, 800, 600, 0), "unchanged screen keeps its surface");
  // Grid: two columns at x 20/415, rows 44 apart from y 122.
  check(screen.box_rect(0).x == 20 && screen.box_rect(0).y == 122 && screen.box_rect(1).x == 415 &&
            screen.box_rect(2).y == 166 && screen.box_rect(13).y == 122 + 6 * 44,
        "option grid");
  check(screen.box_rect(0).width == 80 && screen.box_rect(0).height == 32 && screen.label_rect(0).x == 124,
        "box and label geometry");
  check(screen.tab_rect(0).x == 14 && screen.tab_rect(5).x == 14 + 5 * 108 && screen.tab_rect(0).y == 70, "tab strip");
  check(screen.button_rect(OptionsScreen::continue_hit).y == 520 &&
            screen.button_rect(OptionsScreen::reset_hit).x == 20 &&
            screen.button_rect(OptionsScreen::reset_hit).y == 532 &&
            screen.button_rect(OptionsScreen::home_hit).x == 620,
        "button positions");
  check(std::fabs(screen.button_rect(OptionsScreen::continue_hit).x +
                  screen.button_rect(OptionsScreen::continue_hit).width / 2 - 400) < 1e-9,
        "continue is centered");
  // Rebinding.
  const int up = find(screen, "moveUp");
  click(screen, center(screen, screen.box_rect(up)));
  check(screen.capturing(), "clicking a key box waits for a key");
  screen.update(fonts, 800, 600, 0);
  const auto before = screen.image().pixels;
  check(screen.update(fonts, 800, 600, 400) && screen.image().pixels != before, "[Hit Key] blinks every 400 ms");
  check(!screen.update(fonts, 800, 600, 500) && screen.update(fonts, 800, 600, 800) && screen.image().pixels == before,
        "and only redraws when the phase flips");
  check(screen.key_down(88) && !screen.capturing() && settings.get("moveUp") == 88 && screen.take_changed() &&
            !screen.take_changed(),
        "the next key is bound");
  click(screen, center(screen, screen.box_rect(up)));
  screen.key_down(key_escape);
  check(!screen.capturing() && settings.get("moveUp") == 88 && screen.visible() && !screen.take_changed(),
        "Escape cancels a capture");
  click(screen, center(screen, screen.box_rect(up)));
  const auto elsewhere = center(screen, screen.box_rect(find(screen, "moveLeft")));
  screen.pointer_down(elsewhere);
  check(!screen.capturing(), "any mouse press leaves input mode");
  screen.pointer_up(elsewhere);
  check(screen.capturing() && find(screen, "moveLeft") >= 0, "the click that left it starts the clicked box");
  screen.key_down(88);
  check(settings.get("moveLeft") == 88 && settings.get("moveUp") == 0, "binding a used key unsets the previous action");
  (void)screen.take_changed();
  click(screen, center(screen, screen.box_rect(up)));
  screen.key_down(0);
  check(screen.capturing() && screen.visible(), "keys without a Flash code are ignored");
  screen.pointer_down({1, 1});
  // Rotate keys follow Allow Camera Rotation.
  const int rotate = find(screen, "rotateLeft"), allow = find(screen, "allowRotation");
  click(screen, center(screen, screen.box_rect(rotate)));
  check(!screen.capturing() && !settings.enabled(screen.option(rotate)),
        "rotate keys are disabled while rotation is off");
  click(screen, center(screen, screen.box_rect(allow)));
  check(settings.flag("allowRotation") && screen.take_changed() && settings.enabled(screen.option(rotate)),
        "Allow Camera Rotation enables them");
  click(screen, center(screen, screen.box_rect(rotate)));
  check(screen.capturing(), "enabled rotate key rebinds");
  screen.key_down(key_escape);
  click(screen, center(screen, screen.box_rect(allow)));
  check(!settings.flag("allowRotation"), "choice boxes cycle back");
  // Tooltip on labels.
  const auto label = center(screen, screen.label_rect(up));
  screen.pointer_move(label);
  screen.update(fonts, 800, 600, 1000);
  check(screen.tooltip_visible() && screen.hover() == OptionsScreen::label_hit + up,
        "hovering a label shows its tooltip");
  screen.pointer_move({799, 599});
  screen.update(fonts, 800, 600, 1000);
  check(!screen.tooltip_visible(), "leaving hides it");
  // Tabs, reset, navigation.
  click(screen, center(screen, screen.tab_rect(2)));
  screen.update(fonts, 800, 600, 1000);
  check(screen.tab() == OptionsTab::hotkeys && screen.option_count() == 16, "tab click switches pages");
  const int potion = find(screen, "useHealthPotion");
  click(screen, center(screen, screen.box_rect(potion)));
  screen.key_down(75);
  check(settings.get("useHealthPotion") == 75, "hot key rebinding");
  (void)screen.take_changed();
  click(screen, center(screen, screen.button_rect(OptionsScreen::reset_hit)));
  check(settings.get("useHealthPotion") == 70 && settings.get("moveLeft") == 88 && screen.take_changed(),
        "reset defaults only the visible tab");
  click(screen, center(screen, screen.tab_rect(4)));
  screen.update(fonts, 800, 600, 1000);
  check(screen.tab() == OptionsTab::graphics && screen.option_count() == 9, "graphics tab");
  const int fps = find(screen, "maxFPS");
  click(screen, center(screen, screen.box_rect(fps)));
  check(settings.get("maxFPS") == 120, "Max FPS advances through its list");
  for (int i = 0; i < 5; ++i) click(screen, center(screen, screen.box_rect(fps)));
  check(settings.get("maxFPS") == 30, "and wraps around");
  // Press and release must land on the same control.
  const auto hp = center(screen, screen.box_rect(find(screen, "particles")));
  screen.pointer_move(hp);
  screen.pointer_down(hp);
  screen.pointer_move({5, 5});
  screen.pointer_up({5, 5});
  check(settings.flag("particles"), "dragging off a box cancels the click");
  screen.pointer_move(hp);
  screen.pointer_down(hp);
  screen.focus_lost();
  screen.pointer_up(hp);
  check(settings.flag("particles"), "focus loss cancels a press");
  (void)screen.take_changed();
  // Resize.
  fonts.set_pixel_scale(2);
  check(screen.update(fonts, 1600, 1200, 1000) && screen.image().pixel_scale == 2 && screen.image().width == 1600 &&
            screen.image().height == 1200 && screen.scale() == 2,
        "drawable density");
  check(screen.position().x == 0, "width 1600 at height 1200 fills the window");
  screen.update(fonts, 2000, 1200, 1000);
  check(screen.position().x == 200 && screen.image().width == 2000, "wider window centers the 800 unit content");
  const auto wide = center(screen, screen.box_rect(find(screen, "particles")));
  click(screen, wide);
  check(!settings.flag("particles"), "hit testing follows the centered layout");
  // Keys.
  check(screen.key_down(65) && screen.visible(), "unbound keys are swallowed");
  check(screen.key_down(settings.get("options")) && !screen.visible(), "the Options key closes");
  screen.open(settings);
  screen.update(fonts, 2000, 1200, 0);
  screen.key_down(key_escape);
  check(!screen.visible(), "Escape closes");
  screen.open(settings);
  screen.update(fonts, 2000, 1200, 0);
  click(screen, center(screen, screen.button_rect(OptionsScreen::continue_hit)));
  check(!screen.visible() && !screen.take_home(), "continue closes");
  screen.open(settings);
  screen.update(fonts, 2000, 1200, 0);
  click(screen, center(screen, screen.button_rect(OptionsScreen::home_hit)));
  check(!screen.visible() && screen.take_home() && !screen.take_home(), "back to home closes and signals");
  check(!screen.key_down(65), "closed screen does not take keys");
}
}
int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Options checks require a scratch directory");
    std::filesystem::create_directories(argv[1]);
    settings_rules(argv[1]);
    FontBook fonts;
    screen_behavior(fonts);
    std::cout << "Options checks: " << checks << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
