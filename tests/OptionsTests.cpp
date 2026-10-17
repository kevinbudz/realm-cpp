#include "realm/ui/options/OptionsMenu.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>

namespace {
int checks{};

void check(bool value, const char* text) {
  ++checks;
  if (!value) {
    throw std::runtime_error(text);
  }
}
}

using namespace realm;

namespace {
void settings_rules(const std::filesystem::path& scratch) {
  Settings s;
  check(s.get("moveUp") == 87 && s.get("moveLeft") == 65 && s.get("moveDown") == 83 &&
            s.get("moveRight") == 68,
        "Flash movement defaults");
  check(s.get("escapeToNexus") == 45 && s.get("escapeToNexus2") == 116 && s.get("options") == 79 &&
            s.get("interact") == 48 && s.get("chat") == 13,
        "Flash hot key defaults");
  check(s.get("togglePerformanceStats") == 0 && s.get("toggleCentering") == 0 &&
            s.get("toggleFullscreenMode") == 0,
        "unset by default");
  check(!s.flag("allowRotation") && s.flag("hpBars") && s.flag("particles") &&
            s.flag("allyNotifs") && !s.flag("fullscreenMode"),
        "toggle defaults");
  check(s.flag("toggleBarText") && s.flag("inventorySwap") && s.flag("screenShake") &&
            !s.flag("smartProjectiles") && !s.flag("showStatistics") && !s.flag("allyParticles"),
        "new row defaults");
  check(s.get("maxFPS") == 60 && s.get("cursor") == 4 && s.flag("vsync") && s.flag("playSFX") &&
            s.flag("showTradePopup"),
        "graphics defaults");
  check(!find_option("quality") && !s.values().contains("quality"),
        "the UI Quality row is gone");
  check(s.get("playerAlpha") == 100 && s.flag("fadePlayers") && !s.flag("fadeGuild") &&
            !s.flag("fadeLocked"),
        "player opacity defaults");
  check(std::fabs(s.camera_angle() - 7 * std::numbers::pi / 4) < 1e-12 &&
            s.default_camera_angle() == s.camera_angle(),
        "45 degree default camera angle");
  // Parameters.setKey unsets any other binding of the same key, including the row-less
  // escapeToNexus2.
  s.set_key("moveUp", 83);
  check(s.get("moveUp") == 83 && s.get("moveDown") == 0,
        "binding a taken key unsets its previous owner");
  s.set_key("useSpecial", 116);
  check(s.get("useSpecial") == 116 && s.get("escapeToNexus2") == 0,
        "hidden bindings take part in conflicts");
  s.set_key("togglePerformanceStats", 0);
  check(s.get("toggleCentering") == 0 && s.get("moveUp") == 83,
        "unsetting a key clears nothing else");
  s.set_key("maxFPS", 60);
  s.set_key("moveLeft", 999);
  s.set_key("nonsense", 65);
  check(s.get("maxFPS") == 60 && s.get("moveLeft") == 65,
        "non-key parameters and invalid codes are ignored");
  check(!s.set_value("hpBars", 7) && s.flag("hpBars") && !s.set_value("moveUp", 1) &&
            !s.set_value("hpBars", 1),
        "invalid or unchanged choices report no change");
  check(s.set_value("hpBars", 0) && !s.flag("hpBars") && s.set_value("cursor", -1) &&
            s.get("cursor") == -1,
        "valid choices apply");
  check(s.set_value("defaultCameraAngle", 0) && s.camera_angle() == 0 &&
            s.default_camera_angle() == 0,
        "default camera angle resets the camera angle");
  // Only the open category is reset.
  s.reset_category(OptionsCategory::gameplay);
  check(s.flag("hpBars") && s.get("cursor") == 4 && s.get("moveUp") == 83 &&
            s.get("defaultCameraAngle") == 1,
        "reset affects one category");
  check(s.preferences().hp_bars && s.preferences().ally_notifs && s.preferences().show_trade_popup,
        "preferences follow values");
  s.set_value("allyNotifs", 0);
  s.set_value("particles", 0);
  s.set_value("textBubbles", 0);
  const auto prefs = s.preferences();
  check(!prefs.ally_notifs && !prefs.particles && !prefs.text_bubbles && prefs.hp_bars,
        "preferences expose gated systems");
  s.set_value("screenShake", 0);
  s.set_value("toggleBarText", 0);
  s.set_value("allyParticles", 1);
  s.set_value("smartProjectiles", 1);
  const auto effects = s.preferences();
  check(!effects.screen_shake && !effects.bar_text && effects.ally_particles &&
            effects.smart_projectiles,
        "effect preferences follow values");
  s.set_value("screenShake", 1);
  s.set_value("toggleBarText", 1);
  s.set_value("allyParticles", 0);
  s.set_value("smartProjectiles", 0);
  // Persistence.
  const auto path = scratch / "settings.json";
  s.set_camera_angle(1.25);
  check(s.save(path) && !std::filesystem::exists(path.string() + ".tmp"),
        "atomic save leaves no temporary file");
  const auto loaded = Settings::load(path);
  check(loaded.values() == s.values() && loaded.camera_angle() == 1.25, "settings round trip");
  check(Settings::load(scratch / "missing.json").values() == Settings().values(),
        "missing file yields defaults");
  {
    std::ofstream bad(scratch / "bad.json");
    bad << "{ not json";
  }
  check(Settings::load(scratch / "bad.json").values() == Settings().values(),
        "corrupt file yields defaults");
  {
    std::ofstream odd(scratch / "odd.json");
    odd << R"({"values":{"moveUp":65,"hpBars":7,"maxFPS":144,"nobody":1,"cursor":"x","chat":-5,"particles":0,"quality":0},"cameraAngle":"NaN"})";
  }
  const auto odd = Settings::load(scratch / "odd.json");
  check(odd.get("moveUp") == 65 && odd.get("maxFPS") == 144 && !odd.flag("particles"),
        "valid stored values load");
  check(odd.flag("hpBars") && odd.get("cursor") == 4 && odd.get("chat") == 13 &&
            !odd.values().contains("nobody"),
        "invalid stored values fall back per option");
  // A file saved before the UI Quality row was removed still loads: the old name is dropped.
  check(!odd.values().contains("quality") && odd.get("playerAlpha") == 100,
        "a stored quality value from an older build is dropped");
  check(odd.camera_angle() == odd.default_camera_angle(),
        "missing camera angle falls back to the default");
  check(Settings::path_beside("/a/b/account.json") == "/a/b/account.settings.json",
        "settings sit beside an explicit account file");
  check(!s.save("/proc/forbidden/settings.json"), "unwritable location reports failure");
  // Option table shape: every row in a section, no section split across the table.
  check(category_options(OptionsCategory::keybinds).size() == 34 &&
            category_options(OptionsCategory::gameplay).size() == 14 &&
            category_options(OptionsCategory::video).size() == 15 &&
            category_options(OptionsCategory::audio).size() == 5,
        "category sizes");
  check(category_sections(OptionsCategory::video) ==
            std::vector<std::string_view>{"Display", "Quality", "Allies", "Players"},
        "video sections in order, Players last");
  check(find_option("showTradePopup")->kind == OptionDef::Kind::toggle &&
            find_option("showTradePopup")->category == OptionsCategory::gameplay,
        "Trade Request Panel is a visible Gameplay row");
  check(category_sections(OptionsCategory::keybinds) ==
            std::vector<std::string_view>{"Movement", "Camera", "Combat", "Items", "Interface",
                                          "Chat"},
        "keybind sections in order");
  for (std::size_t c = 0; c < options_category_count; ++c) {
    const auto rows = category_options(static_cast<OptionsCategory>(c));
    std::vector<std::string_view> order;
    for (const auto* row : rows) {
      check(!row->section.empty() && !row->label.empty() && !row->tooltip.empty(),
            "rows have a section, label and description");
      if (order.empty() || order.back() != row->section) {
        check(std::find(order.begin(), order.end(), row->section) == order.end(),
              "sections are contiguous");
        order.push_back(row->section);
      }
    }
  }
  // Sliders.
  const auto& fps = *find_option("maxFPS");
  check(fps.kind == OptionDef::Kind::slider && slider_end(fps) == 1001 &&
            slider_position(fps, 0) == 1001 && slider_value(fps, 1001) == 0 &&
            slider_value(fps, 1000) == 1000 &&
            slider_position(fps, 144) == 144 && slider_value(fps, 10) == 30,
        "Max FPS slider reaches 1000 before its Unlimited stop");
  check(value_text(fps, 0) == "\xE2\x88\x9E" && value_text(fps, 120) == "120" &&
            value_text(*find_option("masterVolume"), 75) == "75%" &&
            value_text(*find_option("playerAlpha"), 40) == "40%" &&
            value_text(*find_option("hpBars"), 1) == "On" &&
            value_text(*find_option("moveUp"), 87) == "W",
        "value text");
  {
    Settings v;
    check(v.get("masterVolume") == 100 && v.set_value("masterVolume", 40) &&
              !v.set_value("masterVolume", 101) && v.set_value("maxFPS", 0) &&
              v.set_value("maxFPS", 200) && !v.set_value("maxFPS", 29),
          "slider values validate against their range");
    v.set_value("sfxVolume", 50);
    const auto p = v.preferences();
    check(std::fabs(p.master_volume - .4F) < 1e-6 && std::fabs(p.sfx_volume - .5F) < 1e-6 &&
              p.weapon_volume == 1,
          "volumes reach the preferences");
    check(v.set_value("playerAlpha", 0) && v.set_value("playerAlpha", 100) &&
              !v.set_value("playerAlpha", 101) && v.set_value("playerAlpha", 40) &&
              std::fabs(v.preferences().player_alpha - .4F) < 1e-6,
          "player opacity validates against 0 to 100 and reaches the preferences");
  }
  std::set<std::string_view> seen;
  for (const auto& option : option_defs()) {
    if (!option.param.empty()) {
      check(seen.insert(option.param).second, "unique parameter names");
    }
  }
  check(key_name(0) == "[Unset]" && key_name(45) == "Insert" && key_name(191) == "/" &&
            key_name(220) == "\\" && key_name(222) == "'" && key_name(33) == "PgUp" &&
            key_name(1).empty(),
        "KeyCodes names");
}

bool listed(const OptionsMenu& menu, std::string_view param) {
  return std::any_of(menu.rows().begin(), menu.rows().end(),
                     [&](const OptionDef* row) { return row->param == param; });
}

void menu_behavior() {
  Settings settings;
  OptionsMenu menu;
  check(!menu.visible() && !menu.key_down(65), "a closed menu takes no keys");
  menu.open(settings);
  check(menu.visible() && menu.category() == OptionsCategory::keybinds &&
            menu.rows().size() == 34 && listed(menu, "moveUp"),
        "opens on Keybinds");
  // Rebinding.
  auto revision = menu.value_revision();
  menu.begin_capture("moveUp");
  check(menu.capturing() && menu.capture() == "moveUp" && menu.value_revision() != revision,
        "a key box waits for a key");
  check(menu.key_down(88) && !menu.capturing() && settings.get("moveUp") == 88 &&
            menu.take_changed() && !menu.take_changed(),
        "the next key is bound");
  menu.begin_capture("moveUp");
  menu.key_down(key_escape);
  check(!menu.capturing() && settings.get("moveUp") == 88 && menu.visible() && !menu.take_changed(),
        "Escape cancels a capture");
  menu.begin_capture("moveLeft");
  menu.key_down(88);
  check(settings.get("moveLeft") == 88 && settings.get("moveUp") == 0,
        "binding a used key unsets the previous action");
  (void)menu.take_changed();
  menu.begin_capture("moveUp");
  menu.key_down(0);
  check(menu.capturing() && menu.visible(), "keys without a Flash code are ignored");
  menu.cancel_capture();
  // Rotate keys follow Allow Camera Rotation, which lives in another category.
  menu.begin_capture("rotateLeft");
  check(!menu.capturing() && !menu.enabled(*find_option("rotateLeft")),
        "rotate keys are disabled while rotation is off");
  menu.toggle("allowRotation");
  check(settings.flag("allowRotation") && menu.take_changed() &&
            menu.enabled(*find_option("rotateLeft")),
        "Allow Camera Rotation enables them");
  menu.begin_capture("rotateLeft");
  check(menu.capturing(), "enabled rotate key rebinds");
  menu.toggle("allowRotation");
  check(!menu.capturing() && !settings.flag("allowRotation"), "changing a value leaves input mode");
  // Values.
  menu.set_value("maxFPS", 0);
  menu.set_value("masterVolume", 250);
  check(settings.get("maxFPS") == 0 && settings.get("masterVolume") == 100 && menu.take_changed(),
        "values apply and invalid ones are ignored");
  menu.set_value("cursor", -1);
  check(settings.get("cursor") == -1, "dropdown values apply");
  (void)menu.take_changed();
  // Categories and reset.
  revision = menu.layout_revision();
  menu.set_category(OptionsCategory::video);
  check(menu.category() == OptionsCategory::video && menu.rows().size() == 15 &&
            menu.layout_revision() != revision && listed(menu, "maxFPS"),
        "category switches the rows");
  menu.set_value("particles", 0);
  menu.reset();
  check(settings.flag("particles") && settings.get("maxFPS") == 60 &&
            settings.get("moveLeft") == 88 && settings.get("cursor") == -1 && menu.take_changed(),
        "reset defaults only the open category");
  // Search.
  menu.set_query("volume");
  check(menu.searching() && menu.rows().size() == 3 && listed(menu, "weaponVolume"),
        "search lists matches from every category");
  menu.set_query("  ");
  check(menu.rows().size() == 15, "a blank query lists the open category");
  menu.set_query("chat scroll");
  check(menu.rows().size() == 2 && listed(menu, "scrollChatUp"),
        "every query word must match (label, section or category)");
  menu.set_query("AUDIO");
  check(menu.rows().size() == 5, "category names match case-insensitively");
  menu.set_query("zzz");
  check(menu.rows().empty(), "no matches");
  menu.set_query("inventory slot");
  menu.begin_capture("useEquipInvSlot1");
  menu.key_down(75);
  menu.reset();
  check(settings.get("useEquipInvSlot1") == 49 && settings.get("moveLeft") == 88,
        "reset while searching defaults only the listed rows");
  menu.set_category(OptionsCategory::audio);
  check(!menu.searching() && menu.rows().size() == 5, "choosing a category ends the search");
  // Closing.
  check(menu.key_down(65) && menu.visible(), "unbound keys are swallowed");
  check(menu.key_down(settings.get("options")) && !menu.visible(), "the Show Settings key closes");
  menu.open(settings);
  check(menu.category() == OptionsCategory::keybinds && !menu.searching(),
        "reopening starts on Keybinds");
  menu.key_down(key_escape);
  check(!menu.visible(), "Escape closes");
  menu.open(settings);
  menu.request_close();
  check(!menu.visible() && !menu.take_home(), "Done closes");
  menu.open(settings);
  menu.request_home();
  check(!menu.visible() && menu.take_home() && !menu.take_home(),
        "Back to Home closes and signals");
}
}

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error("Options checks require a scratch directory");
    }
    std::filesystem::create_directories(argv[1]);
    settings_rules(argv[1]);
    menu_behavior();
    std::cout << "Options checks: " << checks << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
