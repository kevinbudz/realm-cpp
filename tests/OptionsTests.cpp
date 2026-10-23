#include "realm/ui/options/OptionsMenu.hpp"
#include "realm/stage3D/GraphicsApi.hpp"
#include <algorithm>
#include <array>
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
  check(s.get("escapeToNexus") == 82 && s.get("escapeToNexus2") == 116 && s.get("options") == 79 &&
            s.get("interact") == 48 && s.get("chat") == 13,
        "Flash hot key defaults");
  check(s.get("resetToDefaultCameraAngle") == 90 && s.get("toggleCentering") == 88 &&
            s.get("togglePerformanceStats") == 80 && s.get("toggleFullscreenMode") == 122,
        "camera, stats and fullscreen hot key defaults");
  check(s.flag("allowRotation") && s.flag("hpBars") && s.flag("particles") &&
            s.flag("allyNotifs") && !s.flag("fullscreenMode"),
        "toggle defaults");
  check(s.flag("toggleBarText") && s.flag("screenShake") && !s.flag("smartProjectiles") &&
            !s.flag("showStatistics") && s.flag("allyParticles"),
        "new row defaults");
  check(s.get("maxFPS") == 0 && s.get("cursor") == 4 && s.flag("vsync") && s.flag("playSFX") &&
            s.flag("showTradePopup"),
        "graphics defaults");
  // Renderer: the graphics API for the next start. OpenGL by default, a restart-required choice.
  const auto* renderer = find_option("renderer");
  check(renderer && renderer->kind == OptionDef::Kind::choice &&
            renderer->category == OptionsCategory::video && renderer->section == "Display" &&
            renderer->restart && renderer->fallback == 0 &&
            renderer->choices == ChoiceList{{"OpenGL", 0}, {"Vulkan", 1}} &&
            value_text(*renderer, 1) == "Vulkan" &&
            s.get("renderer") == static_cast<int>(gfx::Api::opengl) &&
            !find_option("vsync")->restart,
        "Renderer is a restart-required choice (OpenGL, Vulkan), OpenGL by default");
  Settings renderer_copy = s;
  check(renderer_copy.set_value("renderer", 1) && renderer_copy.get("renderer") == 1 &&
            !renderer_copy.set_value("renderer", 2) && renderer_copy.get("renderer") == 1,
        "Renderer accepts only its two values");
  check(s.get("aspectRatio") == aspect_any && s.get("resolution") == 0 &&
            s.preferences().ally_particles == AllyParticles::all,
        "window size defaults to Any and Custom, and Ally Particles to All");
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
  check(s.get("toggleCentering") == 88 && s.get("moveUp") == 83,
        "unsetting a key clears nothing else");
  s.set_key("maxFPS", 60);
  s.set_key("moveLeft", 999);
  s.set_key("nonsense", 65);
  check(s.get("maxFPS") == 0 && s.get("moveLeft") == 65,
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
  check(!s.flag("hpBars") && s.get("cursor") == 4 && s.get("moveUp") == 83 &&
            s.get("defaultCameraAngle") == 1,
        "reset affects one category");
  // Health Bars is a Video row, so only Video resets it.
  s.reset_category(OptionsCategory::video);
  check(s.flag("hpBars"), "Health Bars resets with Video");
  check(s.preferences().hp_bars && s.preferences().ally_notifs && s.preferences().show_trade_popup,
        "preferences follow values");
  s.set_value("allyNotifs", 0);
  s.set_value("particles", 0);
  s.set_value("textBubbles", 0);
  const auto prefs = s.preferences();
  check(!prefs.ally_notifs && prefs.particles == ParticleQuality::off && !prefs.text_bubbles &&
            prefs.hp_bars,
        "preferences expose gated systems");
  s.set_value("screenShake", 0);
  s.set_value("toggleBarText", 0);
  s.set_value("allyParticles", 1);
  s.set_value("smartProjectiles", 1);
  const auto effects = s.preferences();
  check(!effects.screen_shake && !effects.bar_text &&
            effects.ally_particles == AllyParticles::all && effects.smart_projectiles,
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
  // The Renderer choice persists with the other settings; a stored value outside the choices falls
  // back.
  {
    Settings chosen;
    chosen.set_value("renderer", 1);
    check(chosen.save(scratch / "renderer.json") &&
              Settings::load(scratch / "renderer.json").get("renderer") == 1,
          "Renderer choice persists to the settings file");
    std::ofstream bad_renderer(scratch / "renderer_bad.json");
    bad_renderer << R"({"values":{"renderer":7}})";
    bad_renderer.close();
    check(Settings::load(scratch / "renderer_bad.json").get("renderer") == 0,
          "a stored Renderer value outside its choices falls back to OpenGL");
  }
  check(odd.camera_angle() == odd.default_camera_angle(),
        "missing camera angle falls back to the default");
  // Shift+wheel zoom: a notch is 0.1, rounded to the nearest 0.1 and held to 0.8 to 2.
  check(step_camera_zoom(1.0, 1) == 1.1 && step_camera_zoom(1.0, -1) == 0.9 &&
            step_camera_zoom(1.26, 0) == 1.3 && step_camera_zoom(1.04, 0) == 1.0 &&
            step_camera_zoom(2.0, 1) == 2.0 && step_camera_zoom(0.8, -1) == 0.8 &&
            step_camera_zoom(1.9, 5) == 2.0 && step_camera_zoom(1.0, 0) == 1.0,
        "camera zoom notches are 0.1 and clamp to 0.8 to 2");
  check(step_camera_zoom(std::nan(""), 1) == 1.1 && step_camera_zoom(1.0, std::nan("")) == 1.0,
        "non-finite camera zoom and steps fall back");
  double zoom = 1.0;
  for (int i = 0; i < 12; ++i) {
    zoom = step_camera_zoom(zoom, 1);
  }
  check(zoom == 2.0, "repeated notches stop at 2 without drift");
  for (int i = 0; i < 20; ++i) {
    zoom = step_camera_zoom(zoom, -1);
  }
  check(zoom == 0.8, "repeated notches stop at 0.8 without drift");
  {
    Settings z;
    z.set_camera_zoom(3.0);
    check(z.camera_zoom() == 2.0, "camera zoom set above the range clamps to 2");
    z.set_camera_zoom(0.1);
    check(z.camera_zoom() == 0.8, "camera zoom set below the range clamps to 0.8");
    z.set_camera_zoom(1.7);
    check(z.save(scratch / "zoom.json") &&
              Settings::load(scratch / "zoom.json").camera_zoom() == 1.7,
          "camera zoom round trip");
    z.reset_category(OptionsCategory::video);
    check(z.camera_zoom() == 1.7, "reset of a category keeps the camera zoom");
    z.reset_all();
    check(z.camera_zoom() == 1.7 && z.get("moveUp") == 87,
          "reset of every row keeps the camera zoom");
  }
  // A stored cameraZoom that is not a number, is non-finite or is out of range falls back to 1,
  // and an in-range one rounds to the nearest 0.1.
  for (const auto& [text, expected] : std::initializer_list<std::pair<const char*, double>>{
           {"\"NaN\"", 1.0}, {"5", 1.0}, {"0.5", 1.0}, {"1.23", 1.2}, {"1.6", 1.6}}) {
    {
      std::ofstream zoom_file(scratch / "zoom.json");
      zoom_file << R"({"values":{},"cameraZoom":)" << text << "}";
    }
    check(Settings::load(scratch / "zoom.json").camera_zoom() == expected,
          "stored camera zoom is validated");
  }
  check(Settings::path_beside("/a/b/account.json") == "/a/b/account.settings.json",
        "settings sit beside an explicit account file");
  check(!s.save("/proc/forbidden/settings.json"), "unwritable location reports failure");
  // Option table shape: every row in a section, no section split across the table.
  check(category_options(OptionsCategory::keybinds).size() == 35 &&
            category_options(OptionsCategory::gameplay).size() == 10 &&
            category_options(OptionsCategory::video).size() == 16 &&
            category_options(OptionsCategory::audio).size() == 5,
        "category sizes");
  check(category_sections(OptionsCategory::video) ==
            std::vector<std::string_view>{"Display", "Appearance", "Allies", "Players"},
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
              !v.set_value("masterVolume", 101) && v.set_value("maxFPS", 30) &&
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

// Ally Particles is the first Allies row and maps its saved values: 0 Off, 1 All, 2 Minimal.
void ally_particles_rules(const std::filesystem::path& scratch) {
  const auto* ally = find_option("allyParticles");
  check(ally && ally->kind == OptionDef::Kind::choice && ally->section == "Allies" &&
            ally->category == OptionsCategory::video && ally->fallback == 1,
        "Ally Particles is a video choice in Allies, defaulting to All");
  check(ally->choices == ChoiceList{{"Off", 0}, {"Minimal", 2}, {"All", 1}},
        "Ally Particles lists Off, Minimal and All in that order");
  const auto video = category_options(OptionsCategory::video);
  const auto allies = std::find_if(video.begin(), video.end(),
                                   [](const OptionDef* row) { return row->section == "Allies"; });
  check(allies != video.end() && (*allies)->param == "allyParticles",
        "Ally Particles is the first Allies row");
  Settings s;
  check(s.preferences().ally_particles == AllyParticles::all, "Ally Particles defaults to All");
  check(s.set_value("allyParticles", 0) && s.preferences().ally_particles == AllyParticles::off &&
            s.set_value("allyParticles", 2) &&
            s.preferences().ally_particles == AllyParticles::minimal &&
            s.set_value("allyParticles", 1) && s.preferences().ally_particles == AllyParticles::all,
        "each Ally Particles value reaches the preferences");
  check(!s.set_value("allyParticles", 3) && !s.set_value("allyParticles", -1) &&
            s.get("allyParticles") == 1,
        "Ally Particles rejects values outside its choices");
  {
    std::ofstream file(scratch / "allies.json");
    file << R"({"values":{"allyParticles":2}})";
  }
  check(Settings::load(scratch / "allies.json").preferences().ally_particles ==
            AllyParticles::minimal,
        "a saved Minimal loads");
  {
    std::ofstream file(scratch / "allies.json");
    file << R"({"values":{"allyParticles":7}})";
  }
  check(Settings::load(scratch / "allies.json").preferences().ally_particles == AllyParticles::all,
        "an invalid saved Ally Particles falls back to All");
}

// Shadows is a choice under the saved key drawShadows: a saved 0 is Off and 1 High, the values the
// old toggle wrote, and Minimal is 2.
void shadow_rules(const std::filesystem::path& scratch) {
  const auto* shadows = find_option("drawShadows");
  check(shadows && shadows->kind == OptionDef::Kind::choice && shadows->section == "Appearance" &&
            shadows->category == OptionsCategory::video && shadows->fallback == 1 &&
            shadows->choices == ChoiceList{{"Off", 0}, {"Minimal", 2}, {"High", 1}},
        "Shadows is a video choice in Appearance (Off, Minimal, High), defaulting to High");
  Settings s;
  check(s.preferences().shadows == ShadowQuality::high, "Shadows defaults to High");
  check(s.set_value("drawShadows", 0) && s.preferences().shadows == ShadowQuality::off &&
            s.set_value("drawShadows", 2) && s.preferences().shadows == ShadowQuality::minimal &&
            s.set_value("drawShadows", 1) && s.preferences().shadows == ShadowQuality::high,
        "each Shadows value reaches the preferences");
  check(!s.set_value("drawShadows", 3) && !s.set_value("drawShadows", -1) &&
            s.get("drawShadows") == 1,
        "Shadows rejects values outside its choices");
  for (const auto [saved, expected] :
       {std::pair{0, ShadowQuality::off}, std::pair{1, ShadowQuality::high},
        std::pair{2, ShadowQuality::minimal}}) {
    {
      std::ofstream file(scratch / "shadows.json");
      file << R"({"values":{"drawShadows":)" << saved << "}}";
    }
    check(Settings::load(scratch / "shadows.json").preferences().shadows == expected,
          "a saved Shadows value loads (0 Off, 1 High, 2 Minimal)");
  }
  {
    std::ofstream file(scratch / "shadows.json");
    file << R"({"values":{"drawShadows":7}})";
  }
  check(Settings::load(scratch / "shadows.json").preferences().shadows == ShadowQuality::high,
        "an invalid saved Shadows falls back to High");
}

// Particles is a choice in Appearance under the saved key particles: 0 Off, 1 All and 2 Minimal.
// The old toggle wrote 0 and 1, so a saved file keeps its meaning.
void particle_rules(const std::filesystem::path& scratch) {
  const auto* particles = find_option("particles");
  check(particles && particles->kind == OptionDef::Kind::choice &&
            particles->section == "Appearance" && particles->category == OptionsCategory::video &&
            particles->fallback == 1 &&
            particles->choices == ChoiceList{{"Off", 0}, {"Minimal", 2}, {"All", 1}},
        "Particles is a video choice in Appearance (Off, Minimal, All), defaulting to All");
  Settings s;
  check(s.preferences().particles == ParticleQuality::all, "Particles defaults to All");
  check(s.set_value("particles", 0) && s.preferences().particles == ParticleQuality::off &&
            s.set_value("particles", 2) && s.preferences().particles == ParticleQuality::minimal &&
            s.set_value("particles", 1) && s.preferences().particles == ParticleQuality::all,
        "each Particles value reaches the preferences");
  check(!s.set_value("particles", 3) && !s.set_value("particles", -1) && s.get("particles") == 1,
        "Particles rejects values outside its choices");
  for (const auto [saved, expected] :
       {std::pair{0, ParticleQuality::off}, std::pair{1, ParticleQuality::all},
        std::pair{2, ParticleQuality::minimal}}) {
    {
      std::ofstream file(scratch / "particles.json");
      file << R"({"values":{"particles":)" << saved << "}}";
    }
    check(Settings::load(scratch / "particles.json").preferences().particles == expected,
          "a saved Particles value loads (0 Off, 1 All, 2 Minimal)");
  }
  {
    std::ofstream file(scratch / "particles.json");
    file << R"({"values":{"particles":7}})";
  }
  check(Settings::load(scratch / "particles.json").preferences().particles == ParticleQuality::all,
        "an invalid saved Particles falls back to All");
}

// Aspect Ratio and Resolution: the size table, its labels and value validation, the offer and
// reconcile rules, and the list a menu shows for a display.
void window_size_rules(const std::filesystem::path& scratch) {
  const auto sizes = resolutions();
  check(sizes.size() == 20 && find_resolution(0) == nullptr && find_resolution(-1) == nullptr &&
            find_resolution(21) == nullptr,
        "the size table has 20 entries and no entry for Custom or out of range");
  check(find_resolution(18)->width == 1440 && find_resolution(18)->height == 1080 &&
            find_resolution(18)->aspect == aspect_4_3 && find_resolution(19)->width == 1720 &&
            find_resolution(19)->height == 720 && find_resolution(19)->aspect == aspect_21_9 &&
            find_resolution(20)->width == 1920 && find_resolution(20)->height == 810 &&
            find_resolution(20)->aspect == aspect_21_9,
        "the appended sizes keep their values: 1440 x 1080, 1720 x 720 and 1920 x 810");
  check(find_resolution(1)->width == 800 && find_resolution(1)->height == 600 &&
            find_resolution(17)->width == 3440 && find_resolution(17)->height == 1440 &&
            find_resolution(11)->width == 1366 && find_resolution(11)->aspect == aspect_16_9,
        "table entries keep their order and 1366 x 768 is 16:9");
  // Each entry's aspect is the nearest of the four ratios, and its real ratio is within 3% of it.
  constexpr std::array<std::pair<int, double>, 4> ratios{{{aspect_4_3, 4.0 / 3},
                                                          {aspect_16_10, 1.6},
                                                          {aspect_16_9, 16.0 / 9},
                                                          {aspect_21_9, 21.0 / 9}}};
  for (const auto& size : sizes) {
    const double ratio = static_cast<double>(size.width) / size.height;
    int nearest = aspect_any;
    double gap = 1e9;
    for (const auto& [aspect, nominal] : ratios) {
      if (std::fabs(ratio - nominal) < gap) {
        gap = std::fabs(ratio - nominal);
        nearest = aspect;
      }
    }
    check(size.aspect == nearest && gap / ratio < 0.03,
          "every size is classified as the aspect ratio it is nearest to");
  }
  const auto* resolution = find_option("resolution");
  check(resolution && resolution->choices.size() == 21 &&
            resolution->choices[0] == std::pair<std::string_view, int>{"Custom", 0} &&
            resolution->choices[1].first == "800 \xC3\x97 600" &&
            resolution->choices[4].first == "1440 \xC3\x97 1080" &&
            resolution->choices.back().first == "3440 \xC3\x97 1440" &&
            value_text(*resolution, 0) == "Custom" &&
            value_text(*resolution, 13) == "1920 \xC3\x97 1080",
        "Resolution lists Custom and every table size");
  check(resolution->disabled_by == "fullscreenMode" && resolution->enabled_by.empty(),
        "Resolution is disabled by fullscreen");
  const auto* aspect = find_option("aspectRatio");
  check(aspect && aspect->section == "Display" && aspect->fallback == aspect_any &&
            aspect->choices ==
                ChoiceList{{"Any", 0}, {"16:9", 1}, {"16:10", 2}, {"21:9", 3}, {"4:3", 4}} &&
            value_text(*aspect, 4) == "4:3",
        "Aspect Ratio lists Any, 16:9, 16:10, 21:9 and 4:3");
  const auto video = category_options(OptionsCategory::video);
  check(video.size() >= 6 && video[0]->param == "aspectRatio" && video[1]->param == "resolution" &&
            video[2]->param == "renderer" && video[3]->param == "fullscreenMode" &&
            video[4]->param == "vsync" && video[5]->param == "maxFPS",
        "Display lists Aspect Ratio, Resolution, Renderer, Fullscreen, VSync and Max FPS in order");
  // The dropdown lists the sizes grouped by aspect ratio (4:3, 16:10, 16:9, 21:9), ascending width
  // in each.
  int last_group = -1;
  int last_width = 0;
  for (const auto& [label, value] : resolution->choices) {
    if (value == 0) {
      continue;
    }
    const auto* size = find_resolution(value);
    const int group = size->aspect == aspect_4_3     ? 0
                      : size->aspect == aspect_16_10 ? 1
                      : size->aspect == aspect_16_9  ? 2
                                                     : 3;
    check(group > last_group || (group == last_group && size->width > last_width),
          "the Resolution list is grouped by aspect ratio, ascending width in each");
    last_group = group;
    last_width = size->width;
  }
  const auto gameplay = category_options(OptionsCategory::gameplay);
  check(gameplay.size() >= 3 && gameplay[0]->param == "defaultCameraAngle" &&
            gameplay[1]->param == "allowRotation" && gameplay[2]->param == "centerOnPlayer",
        "Default Camera Angle comes above Allow Camera Rotation");

  Settings s;
  check(s.get("aspectRatio") == 0 && s.get("resolution") == 0, "window size defaults");
  check(s.set_value("aspectRatio", aspect_4_3) && !s.set_value("aspectRatio", 5) &&
            !s.set_value("aspectRatio", -1) && s.get("aspectRatio") == 4,
        "Aspect Ratio accepts its five values only");
  check(s.set_value("resolution", 20) && !s.set_value("resolution", 21) &&
            !s.set_value("resolution", -1) && s.get("resolution") == 20,
        "Resolution accepts Custom and the table only");
  s.set_value("fullscreenMode", 1);
  check(!s.enabled(*resolution) && s.enabled(*aspect), "Resolution is disabled while fullscreen");
  s.set_value("fullscreenMode", 0);
  check(s.enabled(*resolution), "Resolution is enabled in a window");
  s.reset_category(OptionsCategory::video);
  check(s.get("aspectRatio") == 0 && s.get("resolution") == 0,
        "reset of Video restores Any and Custom");
  s.set_value("aspectRatio", aspect_16_9);
  s.set_value("resolution", 13);
  {
    check(s.save(scratch / "window.json") &&
              Settings::load(scratch / "window.json").get("resolution") == 13 &&
              Settings::load(scratch / "window.json").get("aspectRatio") == aspect_16_9,
          "window size settings round trip");
    std::ofstream file(scratch / "window.json");
    file << R"({"values":{"resolution":21,"aspectRatio":9}})";
  }
  check(Settings::load(scratch / "window.json").get("resolution") == 0 &&
            Settings::load(scratch / "window.json").get("aspectRatio") == 0,
        "invalid stored window sizes fall back to Any and Custom");

  // The offer rules: Custom always; a size needs its aspect (Any takes all) and a fit (0 is no
  // limit).
  check(resolution_offered(0, aspect_16_9, 1, 1) && resolution_offered(15, aspect_any, 0, 0) &&
            resolution_offered(13, aspect_16_9, 1920, 1080) &&
            !resolution_offered(13, aspect_16_9, 1919, 1080) &&
            !resolution_offered(15, aspect_any, 1920, 1080) &&
            !resolution_offered(15, aspect_4_3, 0, 0) &&
            !resolution_offered(21, aspect_any, 0, 0) &&
            resolution_offered(20, aspect_21_9, 1920, 1080) &&
            largest_resolution(aspect_4_3, 1920, 1080) == 18,
        "resolution offers follow aspect ratio and display size");
  check(largest_resolution(aspect_16_9, 0, 0) == 15 &&
            largest_resolution(aspect_16_9, 1920, 1080) == 13 &&
            largest_resolution(aspect_16_9, 1000, 700) == 0 &&
            largest_resolution(aspect_4_3, 0, 0) == 4 &&
            largest_resolution(aspect_16_10, 1920, 1080) == 7 &&
            largest_resolution(aspect_21_9, 1920, 1080) == 20 &&
            largest_resolution(aspect_21_9, 0, 0) == 17 &&
            largest_resolution(aspect_any, 1920, 1080) == 13,
        "the largest offered size is chosen, or Custom when none fits");

  // The menu's list for a display, and the reconcile when Aspect Ratio changes.
  Settings settings;
  OptionsMenu menu;
  menu.open(settings);
  menu.set_category(OptionsCategory::video);
  menu.set_display_size(1920, 1080);
  const auto values = [](const ChoiceList& list) {
    std::vector<int> result;
    for (const auto& choice : list) {
      result.push_back(choice.second);
    }
    return result;
  };
  check(values(menu.choices(*resolution)) ==
            std::vector<int>{0, 1, 2, 3, 18, 5, 6, 7, 10, 11, 12, 13, 19, 20},
        "Any lists Custom and the sizes that fit 1920 x 1080, grouped by aspect ratio");
  menu.set_value("aspectRatio", aspect_16_10);
  check(values(menu.choices(*resolution)) == std::vector<int>{0, 5, 6, 7},
        "the Aspect Ratio narrows the Resolution list");
  menu.set_display_size(0, 0);
  menu.set_value("aspectRatio", aspect_any);
  check(menu.choices(*resolution).size() == 21, "an unknown display size does not limit the list");
  menu.set_display_size(1920, 1080);
  menu.set_value("resolution", 14);
  menu.set_value("aspectRatio", aspect_16_10);
  check(settings.get("resolution") == 7,
        "a size the new ratio does not offer moves to the largest that fits");
  menu.set_value("aspectRatio", aspect_21_9);
  check(settings.get("resolution") == 20,
        "the largest 21:9 size that fits 1920 x 1080 is 1920 x 810");
  menu.set_display_size(1000, 700);
  menu.set_value("aspectRatio", aspect_16_10);
  check(settings.get("resolution") == 0, "Custom when no size of the new ratio fits");
  menu.set_display_size(1920, 1080);
  menu.set_value("aspectRatio", aspect_16_9);
  menu.set_value("resolution", 14);
  menu.set_value("aspectRatio", aspect_any);
  menu.set_value("aspectRatio", aspect_16_9);
  check(settings.get("resolution") == 14, "a size the ratio still offers is kept");
  // A stored size the display cannot fit stays listed, so the dropdown shows the saved value.
  menu.set_display_size(1366, 768);
  check(values(menu.choices(*resolution)) == std::vector<int>{0, 10, 11, 14},
        "a stored size the display cannot fit stays listed");
  settings.set_value("fullscreenMode", 1);
  menu.set_value("resolution", 3);
  check(settings.get("resolution") == 14, "a fullscreen Resolution ignores the menu");
  menu.close();
}

// Toggle Health Bars is an Interface keybind right after Toggle Performance Stats. Its default is H
// (72), the only key default that uses 72.
void hp_bar_key_rules() {
  const auto* hp_key = find_option("toggleHpBars");
  check(hp_key && hp_key->kind == OptionDef::Kind::key &&
            hp_key->category == OptionsCategory::keybinds && hp_key->section == "Interface" &&
            hp_key->label == "Toggle Health Bars" &&
            hp_key->tooltip == "Shows or hides health bars below players and enemies." &&
            hp_key->fallback == 72 && Settings().get("toggleHpBars") == 72 &&
            value_text(*hp_key, 72) == "H",
        "Toggle Health Bars is an Interface keybind with default H (72)");
  const auto rows = category_options(OptionsCategory::keybinds);
  const auto at = std::find(rows.begin(), rows.end(), hp_key);
  check(at != rows.end() && at != rows.begin() && (*(at - 1))->param == "togglePerformanceStats",
        "Toggle Health Bars follows Toggle Performance Stats");
  int defaults = 0;
  for (const auto& option : option_defs()) {
    if ((option.kind == OptionDef::Kind::key || option.kind == OptionDef::Kind::hidden_key) &&
        option.fallback == 72) {
      ++defaults;
    }
  }
  check(defaults == 1, "no other key row defaults to H (72)");
  Settings s;
  s.set_key("toggleHpBars", 75);
  check(s.get("toggleHpBars") == 75 && s.get("togglePerformanceStats") == 80,
        "Toggle Health Bars rebinds like any other key");
  s.set_key("moveUp", 75);
  check(s.get("moveUp") == 75 && s.get("toggleHpBars") == 0,
        "binding a key taken by Toggle Health Bars unsets it");
}

// Appearance lists Shadows, Particles, Quality, Screen Shake, Health Bars and one Projectile row
// (Outlines and Direction boxes). Text Bubbles sits under HP/MP Text in Gameplay's Interface.
// Ctrl-Click Backpack Swap is gone, and a saved file that still names it loads without it.
void row_layout_rules(const std::filesystem::path& scratch) {
  const auto video = category_options(OptionsCategory::video);
  const auto gameplay = category_options(OptionsCategory::gameplay);
  const auto section_params = [](const std::vector<const OptionDef*>& rows, std::string_view name) {
    std::vector<std::string_view> params;
    for (const auto* row : rows) {
      if (row->section == name) {
        params.push_back(row->param);
      }
    }
    return params;
  };
  check(section_params(video, "Appearance") ==
            std::vector<std::string_view>{"drawShadows", "particles", "uiQuality", "screenShake",
                                          "hpBars", "projectileRow"},
        "Appearance lists Shadows, Particles, Quality, Screen Shake, Health Bars and the "
        "Projectile row in that order");
  check(find_option("uiQuality")->label == "Quality" && find_option("uiQuality")->fallback == 1,
        "the UI Quality row is labelled Quality and still defaults to High");
  const auto* projectile = find_option("projectileRow");
  check(projectile && projectile->kind == OptionDef::Kind::checks &&
            projectile->category == OptionsCategory::video &&
            projectile->section == "Appearance" && projectile->label == "Projectile.." &&
            projectile->tooltip ==
                "Outlines draws an outline around projectiles. Direction turns projectiles to face "
                "the way they are moving." &&
            projectile->boxes.size() == 2 && projectile->boxes[0].label == "Outlines" &&
            projectile->boxes[0].param == "projOutline" &&
            projectile->boxes[1].label == "Direction" &&
            projectile->boxes[1].param == "smartProjectiles",
        "the Projectile row is a checks row in Appearance with Outlines and Direction boxes");
  check(find_option("projOutline")->kind == OptionDef::Kind::hidden_value &&
            find_option("projOutline")->fallback == 1 &&
            find_option("smartProjectiles")->kind == OptionDef::Kind::hidden_value &&
            find_option("smartProjectiles")->fallback == 0 && Settings().flag("projOutline") &&
            !Settings().flag("smartProjectiles"),
        "Projectile Outline stays on and Projectile Direction off, as hidden values");
  check(
      section_params(gameplay, "Interface") ==
          std::vector<std::string_view>{"toggleBarText", "textBubbles", "showTierTag",
                                        "showQuestPortraits"},
      "Gameplay Interface lists HP/MP Text, Text Bubbles, Show Tier Tags and Show Quest Portraits");
  check(find_option("hpBars")->category == OptionsCategory::video &&
            find_option("projOutline")->category == OptionsCategory::video &&
            find_option("smartProjectiles")->category == OptionsCategory::video,
        "the moved switches are Video rows");
  check(find_option("inventorySwap") == nullptr && !Settings().values().contains("inventorySwap"),
        "Ctrl-Click Backpack Swap is gone");
  {
    std::ofstream file(scratch / "retired.json");
    file << R"({"values":{"inventorySwap":0,"hpBars":0}})";
  }
  const auto retired = Settings::load(scratch / "retired.json");
  check(!retired.values().contains("inventorySwap") && retired.get("hpBars") == 0 &&
            retired.flag("toggleBarText"),
        "a saved file that names the retired inventorySwap loads and keeps its other values");
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
            menu.rows().size() == 35 && listed(menu, "moveUp"),
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
  // Allow Camera Rotation starts on, so turn it off to check the disabled state.
  menu.toggle("allowRotation");
  (void)menu.take_changed();
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
  check(menu.category() == OptionsCategory::video && menu.rows().size() == 16 &&
            menu.layout_revision() != revision && listed(menu, "maxFPS"),
        "category switches the rows");
  menu.set_value("particles", 0);
  menu.reset();
  check(settings.flag("particles") && settings.get("maxFPS") == 0 &&
            settings.get("moveLeft") == find_option("moveLeft")->fallback &&
            settings.get("cursor") == find_option("cursor")->fallback && menu.take_changed(),
        "reset defaults every category");
  menu.set_value("masterVolume", 50);
  (void)menu.take_changed();
  // Search.
  menu.set_query("volume");
  check(menu.searching() && menu.rows().size() == 3 && listed(menu, "weaponVolume"),
        "search lists matches from every category");
  menu.set_query("  ");
  check(menu.rows().size() == 16, "a blank query lists the open category");
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
  check(settings.get("useEquipInvSlot1") == 49 && settings.get("masterVolume") == 50,
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

// Checks rows: the Show row under Allies and the Apply to row under Players. Each is one line of
// boxes, and each box flips its own 0/1 parameter, a hidden row that is not listed. Performance
// Stats is a hidden row too: it has no screen row, and togglePerformanceStats still flips it.
void checks_rules(const std::filesystem::path& scratch) {
  using Boxes = std::vector<std::pair<std::string_view, std::string_view>>;
  const auto boxes_of = [](const OptionDef& row) {
    Boxes result;
    for (const auto& box : row.boxes) {
      result.emplace_back(box.label, box.param);
    }
    return result;
  };
  const auto* show = find_option("allyShowRow");
  check(show && show->kind == OptionDef::Kind::checks &&
            show->category == OptionsCategory::video && show->section == "Allies" &&
            show->label == "Show.." &&
            show->tooltip == "Other players' projectiles, damage text and notifications. Turn off "
                             "to improve performance." &&
            boxes_of(*show) == Boxes{{"Shots", "allyShots"},
                                     {"Damage", "allyDamage"},
                                     {"Notifications", "allyNotifs"}},
        "the Show row is a checks row in Allies with Shots, Damage and Notifications boxes");
  const auto* apply = find_option("fadeApplyRow");
  check(apply && apply->kind == OptionDef::Kind::checks &&
            apply->category == OptionsCategory::video && apply->section == "Players" &&
            apply->label == "Apply to.." &&
            apply->tooltip == "Which players Player Opacity fades: unlocked players outside your "
                              "guild, guild members, and locked players." &&
            boxes_of(*apply) == Boxes{{"Unlocked", "fadePlayers"},
                                      {"Guild", "fadeGuild"},
                                      {"Locked", "fadeLocked"}},
        "the Apply to row is a checks row in Players with Unlocked, Guild and Locked boxes");
  const auto video = category_options(OptionsCategory::video);
  const auto params_in = [&](std::string_view section) {
    std::vector<std::string_view> params;
    for (const auto* row : video) {
      if (row->section == section) {
        params.push_back(row->param);
      }
    }
    return params;
  };
  check(params_in("Allies") == std::vector<std::string_view>{"allyParticles", "allyShowRow"} &&
            params_in("Players") == std::vector<std::string_view>{"playerAlpha", "fadeApplyRow"},
        "Allies ends with the Show row and Players with Apply to, under their slider or choice");
  const auto row_listed = [&](std::string_view param) {
    return std::any_of(video.begin(), video.end(),
                       [&](const OptionDef* row) { return row->param == param; });
  };
  check(!row_listed("allyShots") && !row_listed("allyDamage") && !row_listed("allyNotifs") &&
            !row_listed("fadePlayers") && !row_listed("fadeGuild") && !row_listed("fadeLocked") &&
            !row_listed("showStatistics") && row_listed("allyShowRow") &&
            row_listed("fadeApplyRow"),
        "the boxes and Performance Stats have no rows of their own");
  // Box kinds and defaults: Shots, Damage, Notifications and Unlocked on; Guild and Locked off.
  for (const auto* box : {"allyShots", "allyDamage", "allyNotifs", "fadePlayers", "fadeGuild",
                          "fadeLocked", "showStatistics"}) {
    const auto* option = find_option(box);
    check(option && option->kind == OptionDef::Kind::hidden_value &&
              option->category == OptionsCategory::video,
          "a box is a hidden video value");
  }
  Settings s;
  check(s.flag("allyShots") && s.flag("allyDamage") && s.flag("allyNotifs") &&
            s.flag("fadePlayers") && !s.flag("fadeGuild") && !s.flag("fadeLocked") &&
            !s.flag("showStatistics"),
        "box defaults: Shots, Damage, Notifications and Unlocked on; Guild, Locked and Performance "
        "Stats off");
  check(!s.set_value("allyShowRow", 1) && !s.set_value("allyShowRow", 0) &&
            !s.values().contains("allyShowRow") && !s.values().contains("fadeApplyRow") &&
            Settings().get("allyShowRow") == 0,
        "a checks row is not a stored value");
  check(s.set_value("allyDamage", 0) && !s.flag("allyDamage") && !s.preferences().ally_damage &&
            s.set_value("allyDamage", 1) && s.preferences().ally_damage &&
            !s.set_value("allyShots", 2) && s.flag("allyShots"),
        "a box flips its own parameter, which accepts 0 and 1 only");
  check(s.set_value("fadeGuild", 1) && s.preferences().fade_guild && s.preferences().fade_players &&
            !s.preferences().fade_locked && s.set_value("fadeLocked", 1) &&
            s.preferences().fade_locked,
        "Apply to boxes reach the preferences");
  check(s.set_value("showStatistics", 1) && s.flag("showStatistics") &&
            !Settings().flag("showStatistics"),
        "Performance Stats is set by value, with its default off");
  // Saved and loaded like any other value. A saved checks row id is dropped.
  check(s.set_value("allyShots", 0) && s.save(scratch / "boxes.json"), "boxes save");
  {
    std::ofstream file(scratch / "ids.json");
    file << R"({"values":{"allyShots":0,"fadeLocked":1,"allyShowRow":0,"fadeApplyRow":1}})";
  }
  const auto boxes = Settings::load(scratch / "boxes.json");
  check(!boxes.flag("allyShots") && boxes.flag("allyDamage") && boxes.flag("showStatistics") &&
            boxes.values().contains("allyShots") && boxes.flag("fadeGuild"),
        "saved boxes load with their values");
  const auto ids = Settings::load(scratch / "ids.json");
  check(ids.get("allyShots") == 0 && ids.flag("fadeLocked") &&
            !ids.values().contains("allyShowRow") && !ids.values().contains("fadeApplyRow"),
        "a saved checks row id is dropped and the boxes load");
  // Category reset restores the boxes (and Performance Stats) with their category, not the rows.
  Settings r;
  r.set_value("allyShots", 0);
  r.set_value("fadeGuild", 1);
  r.set_value("showStatistics", 1);
  r.reset_category(OptionsCategory::gameplay);
  check(!r.flag("allyShots") && r.flag("fadeGuild") && r.flag("showStatistics"),
        "reset of another category keeps the boxes");
  r.reset_category(OptionsCategory::video);
  check(r.flag("allyShots") && !r.flag("fadeGuild") && !r.flag("showStatistics") &&
            r.values().contains("allyShots") && !r.values().contains("allyShowRow"),
        "reset of Video restores the boxes and Performance Stats");
  // The menu: a box click applies live, the Show row matches by its boxes, and a search reset
  // restores only the listed row's boxes.
  Settings ms;
  OptionsMenu menu;
  menu.open(ms);
  menu.set_category(OptionsCategory::video);
  check(listed(menu, "allyShowRow") && listed(menu, "fadeApplyRow") && !listed(menu, "allyShots") &&
            !listed(menu, "showStatistics"),
        "the checks rows are listed and their boxes are not");
  menu.toggle("allyDamage");
  check(!ms.flag("allyDamage") && ms.flag("allyShots") && ms.flag("allyNotifs") &&
            menu.take_changed(),
        "a box click flips only its parameter and reports a change");
  menu.set_query("notifications");
  check(menu.rows().size() == 1 && listed(menu, "allyShowRow"),
        "search matches a checks row by its box labels");
  menu.set_query("unlocked");
  check(menu.rows().size() == 1 && listed(menu, "fadeApplyRow"),
        "search matches Apply to by its Unlocked box");
  menu.set_query("show shots");
  check(menu.rows().size() == 1 && listed(menu, "allyShowRow"),
        "search words match the row title and a box label together");
  menu.toggle("fadeGuild");
  menu.toggle("allyShots");
  menu.set_query("shots");
  menu.reset();
  check(ms.flag("allyShots") && ms.flag("allyDamage") && ms.flag("allyNotifs") &&
            ms.flag("fadeGuild") && menu.take_changed(),
        "reset while searching restores the listed row's boxes and leaves Apply to alone");
  menu.close();
}
}

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error("Options checks require a scratch directory");
    }
    std::filesystem::create_directories(argv[1]);
    settings_rules(argv[1]);
    ally_particles_rules(argv[1]);
    shadow_rules(argv[1]);
    particle_rules(argv[1]);
    window_size_rules(argv[1]);
    hp_bar_key_rules();
    row_layout_rules(argv[1]);
    checks_rules(argv[1]);
    menu_behavior();
    std::cout << "Options checks: " << checks << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
