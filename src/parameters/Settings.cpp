#include "realm/parameters/Settings.hpp"
#include "realm/stage3D/GraphicsApi.hpp"
#include "realm/appengine/SavedCharactersList.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <numbers>

namespace realm {
namespace {
using K = OptionDef::Kind;
using C = OptionsCategory;
constexpr int a_ = 65;
constexpr int d_ = 68;
constexpr int e_ = 69;
constexpr int q_ = 81;
constexpr int s_ = 83;
constexpr int w_ = 87;

// Rows are added one section at a time, so every helper takes the category and section.
struct Section {
  C category;
  std::string_view name;
};

OptionDef row(Section at, K kind, std::string_view param, std::string_view label,
              std::string_view tip, int value) {
  OptionDef option;
  option.category = at.category;
  option.section = at.name;
  option.kind = kind;
  option.param = param;
  option.label = label;
  option.tooltip = tip;
  option.fallback = value;
  return option;
}

OptionDef key(Section at, std::string_view param, std::string_view label, std::string_view tip,
              int code, std::string_view enabled_by = {}) {
  auto option = row(at, K::key, param, label, tip, code);
  option.enabled_by = enabled_by;
  return option;
}

OptionDef toggle(Section at, std::string_view param, std::string_view label,
                 std::string_view tip, bool value = true) {
  return row(at, K::toggle, param, label, tip, value ? 1 : 0);
}

OptionDef choice(Section at, std::string_view param, std::string_view label,
                 std::string_view tip, int value,
                 std::vector<std::pair<std::string_view, int>> choices) {
  auto option = row(at, K::choice, param, label, tip, value);
  option.choices = std::move(choices);
  return option;
}

OptionDef slider(Section at, std::string_view param, std::string_view label,
                 std::string_view tip, int value, int min, int max, int step,
                 std::string_view unit = {}, std::string_view top_label = {}) {
  auto option = row(at, K::slider, param, label, tip, value);
  option.min = min;
  option.max = max;
  option.step = step;
  option.unit = unit;
  option.top_label = top_label;
  return option;
}

// A row of checkboxes. The id names the row (its param is never stored); each box flips its own
// hidden_value parameter.
OptionDef checks(Section at, std::string_view id, std::string_view label, std::string_view tip,
                 std::vector<CheckBox> boxes) {
  auto option = row(at, K::checks, id, label, tip, 0);
  option.boxes = std::move(boxes);
  return option;
}

// A 0/1 parameter with no row of its own: it keeps its default, saved value and consumers, and a
// checks row or a row shown elsewhere flips it.
OptionDef hidden_value(Section at, std::string_view param, std::string_view label, int value) {
  return row(at, K::hidden_value, param, label, {}, value);
}

std::vector<OptionDef> build() {
  std::vector<OptionDef> options;
  const auto add = [&](OptionDef option) { options.push_back(std::move(option)); };
  // Options.addControlsOptions, addHotKeysOptions and addChatOptions.
  Section at{C::keybinds, "Movement"};
  add(key(at, "moveUp", "Move Up", "Moves your character up.", w_));
  add(key(at, "moveLeft", "Move Left", "Moves your character to the left.", a_));
  add(key(at, "moveDown", "Move Down", "Moves your character down.", s_));
  add(key(at, "moveRight", "Move Right", "Moves your character to the right.", d_));
  at.name = "Camera";
  add(key(at, "rotateLeft", "Rotate Left",
          "Rotates the camera to the left. Needs Allow Camera Rotation (Gameplay).", q_,
          "allowRotation"));
  add(key(at, "rotateRight", "Rotate Right",
          "Rotates the camera to the right. Needs Allow Camera Rotation (Gameplay).", e_,
          "allowRotation"));
  add(key(at, "resetToDefaultCameraAngle", "Reset Camera Angle",
          "Returns the camera to its default angle.", 90));
  add(key(at, "toggleCentering", "Toggle Centering",
          "Switches the view between centered on your character and offset.", 88));
  at.name = "Combat";
  add(key(at, "useSpecial", "Use Special Ability", "Activates your special ability.", 32));
  add(key(at, "autofireToggle", "Autofire Toggle", "Turns autofire on and off.", 73));
  add(key(at, "interact", "Interact / Buy", "Enters a portal or buys the item you stand on.",
          48));
  at.name = "Items";
  add(key(at, "useHealthPotion", "Use Health Potion", "Drinks a health potion if you have one.",
          70));
  add(key(at, "useMagicPotion", "Use Magic Potion", "Drinks a magic potion if you have one.", 86));
  static const std::array<std::string, 8> slot_params{
      "useEquipInvSlot1", "useEquipInvSlot2", "useEquipInvSlot3", "useEquipInvSlot4",
      "useEquipInvSlot5", "useEquipInvSlot6", "useEquipInvSlot7", "useEquipInvSlot8"};
  static const std::array<std::string, 8> slot_labels{
      "Inventory Slot 1", "Inventory Slot 2", "Inventory Slot 3", "Inventory Slot 4",
      "Inventory Slot 5", "Inventory Slot 6", "Inventory Slot 7", "Inventory Slot 8"};
  static const std::array<std::string, 8> slot_tips{
      "Uses or equips the item in inventory slot 1.", "Uses or equips the item in inventory slot 2.",
      "Uses or equips the item in inventory slot 3.", "Uses or equips the item in inventory slot 4.",
      "Uses or equips the item in inventory slot 5.", "Uses or equips the item in inventory slot 6.",
      "Uses or equips the item in inventory slot 7.", "Uses or equips the item in inventory slot 8."};
  for (std::size_t i = 0; i < 8; ++i) {
    add(key(at, slot_params[i], slot_labels[i], slot_tips[i], 49 + static_cast<int>(i)));
  }
  at.name = "Interface";
  add(key(at, "escapeToNexus", "Escape To Nexus", "Instantly returns you to the Nexus.", 82));
  add(key(at, "options", "Show Settings", "Opens this screen.", 79));
  add(key(at, "switchTabs", "Switch Tabs", "Cycles through the available inventory tabs.", 66));
  add(key(at, "miniMapZoomIn", "Minimap Zoom In", "Zooms the minimap in.", 187));
  add(key(at, "miniMapZoomOut", "Minimap Zoom Out", "Zooms the minimap out.", 189));
  add(key(at, "toggleFullscreenMode", "Toggle Fullscreen", "Switches fullscreen on and off.", 122));
  add(key(at, "togglePerformanceStats", "Toggle Performance Stats",
          "Shows or hides the frame rate and memory display.", 80));
  add(key(at, "toggleHpBars", "Toggle Health Bars",
          "Shows or hides health bars below players and enemies.", 72));
  at.name = "Chat";
  add(key(at, "chat", "Open Chat", "Opens the chat input box.", 13));
  add(key(at, "chatCommand", "Start Chat Command",
          "Opens chat with a '/' already typed, for commands such as /who and /ignore.", 191));
  add(key(at, "tell", "Begin Tell", "Opens chat to send a private message.", 9));
  add(key(at, "guildChat", "Begin Guild Chat", "Opens chat to talk to your guild.", 71));
  add(key(at, "scrollChatUp", "Scroll Chat Up", "Scrolls to older chat messages.", 33));
  add(key(at, "scrollChatDown", "Scroll Chat Down", "Scrolls to newer chat messages.", 34));
  // Options.addControlsOptions (camera rotation) and addGameplayOptions.
  at = {C::gameplay, "Camera"};
  add(choice(at, "defaultCameraAngle", "Default Camera Angle",
             "The angle the camera starts at and resets to.", 1,
             {{"45\xC2\xB0", 1}, {"0\xC2\xB0", 0}}));
  add(toggle(at, "allowRotation", "Allow Camera Rotation",
             "Lets the rotate keys turn the camera."));
  add(toggle(at, "centerOnPlayer", "Center On Player",
             "Keeps your character in the middle of the screen instead of offset."));
  at.name = "Interface";
  add(toggle(at, "toggleBarText", "HP/MP Text",
             "Shows the HP and MP numbers on the bars. Off shows them only while the pointer is "
             "over a bar."));
  add(toggle(at, "textBubbles", "Text Bubbles", "Draws chat speech bubbles above players."));
  add(toggle(at, "showTierTag", "Show Tier Tags", "Shows tier tags on your gear."));
  add(toggle(at, "showQuestPortraits", "Show Quest Portraits",
             "Shows the portrait of your current quest target."));
  std::vector<std::pair<std::string_view, int>> cursors{{"System", -1}};
  static const std::array<std::string, 16> cursor_labels{
      "Cursor 0", "Cursor 1", "Cursor 2",  "Cursor 3",  "Cursor 4",  "Cursor 5",
      "Cursor 6", "Cursor 7", "Cursor 8",  "Cursor 9",  "Cursor 10", "Cursor 11",
      "Cursor 12", "Cursor 13", "Cursor 14", "Cursor 15"};
  for (int i = 0; i < 16; ++i) {
    cursors.emplace_back(cursor_labels[static_cast<std::size_t>(i)], i);
  }
  at.name = "Social";
  add(toggle(at, "showTradePopup", "Trade Request Panel",
             "Shows trade requests in the lower-right panel instead of only in chat."));
  add(toggle(at, "showGuildInvitePopup", "Guild Invite Panel",
             "Shows guild invites in the lower-right panel instead of only in chat."));
  at.name = "Cursor";
  auto cursor = choice(at, "cursor", "Mouse Cursor", "The cursor shown over the game.", 4,
                       std::move(cursors));
  cursor.preview = "cursors";
  add(std::move(cursor));
  // Options.addGraphicsOptions. There is no Hardware Acceleration row: the native renderer is
  // always the GPU path.
  at = {C::video, "Display"};
  add(choice(at, "aspectRatio", "Aspect Ratio",
             "Limits the Resolution list to window sizes of one aspect ratio.", aspect_any,
             {{"Any", aspect_any},
              {"16:9", aspect_16_9},
              {"16:10", aspect_16_10},
              {"21:9", aspect_21_9},
              {"4:3", aspect_4_3}}));
  // One label per size, built once so the choice's string_views stay valid.
  static const std::vector<std::string> size_labels = [] {
    std::vector<std::string> labels;
    for (const auto& size : resolutions()) {
      labels.push_back(std::to_string(size.width) + " \xC3\x97 " + std::to_string(size.height));
    }
    return labels;
  }();
  // The dropdown lists sizes grouped by aspect ratio (4:3, 16:10, 16:9, 21:9), each by ascending
  // width. Values stay in table order, so the list order is not the value order.
  constexpr std::array<int, 4> aspect_order{aspect_4_3, aspect_16_10, aspect_16_9, aspect_21_9};
  const auto table = resolutions();
  std::vector<std::size_t> order(table.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  const auto group = [&](std::size_t index) {
    return std::find(aspect_order.begin(), aspect_order.end(), table[index].aspect) -
           aspect_order.begin();
  };
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return group(a) != group(b) ? group(a) < group(b) : table[a].width < table[b].width;
  });
  ChoiceList sizes{{"Custom", 0}};
  for (const auto index : order) {
    sizes.emplace_back(size_labels[index], static_cast<int>(index) + 1);
  }
  // Fullscreen sets the screen size, so this row is disabled then.
  auto resolution =
      choice(at, "resolution", "Resolution",
             "Window size in windowed mode. Choose an aspect ratio to narrow the list.", 0,
             std::move(sizes));
  resolution.disabled_by = "fullscreenMode";
  add(std::move(resolution));
  // The graphics API the window opens with, read once at startup. It sits after Resolution, with
  // the other window settings.
  auto renderer = choice(at, "renderer", "Renderer",
                         "Graphics API for the game window: OpenGL or Vulkan. Takes effect after "
                         "you restart the client.",
                         static_cast<int>(gfx::Api::opengl),
                         {{"OpenGL", static_cast<int>(gfx::Api::opengl)},
                          {"Vulkan", static_cast<int>(gfx::Api::vulkan)}});
  renderer.restart = true;
  add(std::move(renderer));
  add(toggle(at, "fullscreenMode", "Fullscreen", "Fills the whole screen with the game.", false));
  add(toggle(at, "vsync", "VSync",
             "Synchronizes rendering with the display refresh to prevent screen tearing."));
  add(slider(at, "maxFPS", "Max FPS",
             "Caps the frame rate. Unlimited renders as fast as possible (up to your display "
             "refresh with VSync on).",
             0, 30, 1000, 1, {}, "\xE2\x88\x9E"));
  // Performance Stats has no row: togglePerformanceStats flips it and the overlay follows.
  add(hidden_value(at, "showStatistics", "Performance Stats", 0));
  at.name = "Appearance";
  // Live: read each frame. Saved 0 is Off and 1 High, as the old toggle wrote them; Minimal is 2.
  add(choice(at, "drawShadows", "Shadows",
             "Draws shadows under players, enemies and objects. Minimal skips projectile shadows.",
             1, {{"Off", 0}, {"Minimal", 2}, {"High", 1}}));
  // Live: read by the particle system. Saved 0 is Off, 1 All and 2 Minimal, so a saved toggle keeps
  // its meaning (false Off, true All).
  add(choice(
      at, "particles", "Particles",
      "Off renders no particles. Minimal keeps only gameplay effects (heals, status, ability "
      "areas, grenade and thrown telegraphs) at reduced density. All renders everything.",
      1, {{"Off", 0}, {"Minimal", 2}, {"All", 1}}));
  // World text at the camera zoom (names, signs, damage text, speech balloons). Live: no restart.
  add(choice(at, "uiQuality", "Quality",
             "Sharpness of names, signs, damage and balloons when zoomed. High redraws them "
             "at each zoom step; Low scales one bitmap, so zoom steps cost nothing.",
             1, {{"Low", 0}, {"High", 1}}));
  // Native addition: BetterSkillys has no screen shake option.
  add(toggle(at, "screenShake", "Screen Shake", "Shakes the screen during some effects."));
  add(toggle(at, "hpBars", "Health Bars", "Draws health bars below players and enemies."));
  // Projectile Outline and Projectile Direction share one row of boxes, as the Allies Show row
  // does. Their saved keys keep their defaults: outline on, direction off.
  add(hidden_value(at, "projOutline", "Projectile Outline", 1));
  add(hidden_value(at, "smartProjectiles", "Projectile Direction", 0));
  add(checks(at, "projectileRow", "Projectile..",
             "Outlines draws an outline around projectiles. Direction turns projectiles to face "
             "the way they are moving.",
             {{"Outlines", "projOutline"}, {"Direction", "smartProjectiles"}}));
  at.name = "Allies";
  // Options.addMiscOptions: Disable Ally Particles, now a choice. Its saved 0 and 1 still mean Off
  // and All.
  add(choice(at, "allyParticles", "Ally Particles",
             "Particle effects from other players. Minimal keeps heals, ability areas and flashes; "
             "Off hides them all.",
             1, {{"Off", 0}, {"Minimal", 2}, {"All", 1}}));
  // The Show row's boxes keep their own saved values; the row shows them as one line of checkboxes.
  add(hidden_value(at, "allyShots", "Ally Shots", 1));
  add(hidden_value(at, "allyDamage", "Ally Damage", 1));
  add(hidden_value(at, "allyNotifs", "Ally Notifications", 1));
  add(checks(at, "allyShowRow", "Show..",
             "Other players' projectiles, damage text and notifications. Turn off to improve "
             "performance.",
             {{"Shots", "allyShots"}, {"Damage", "allyDamage"}, {"Notifications", "allyNotifs"}}));
  // Native addition: BetterSkillys hides players by category ("Hide Players"). These fade them
  // instead, each category with its own box.
  at.name = "Players";
  add(slider(at, "playerAlpha", "Player Opacity",
             "Fades the players picked under Apply to. Lower values are more see-through; 0% hides "
             "them.",
             100, 0, 100, 1, "%"));
  add(hidden_value(at, "fadePlayers", "Fade Other Players", 1));
  add(hidden_value(at, "fadeGuild", "Fade Guild Members", 0));
  add(hidden_value(at, "fadeLocked", "Fade Locked Players", 0));
  add(checks(at, "fadeApplyRow", "Apply to..",
             "Which players Player Opacity fades: unlocked players outside your guild, guild "
             "members, and locked players.",
             {{"Unlocked", "fadePlayers"}, {"Guild", "fadeGuild"}, {"Locked", "fadeLocked"}}));
  // Options.addSoundOptions, plus native volume sliders.
  at = {C::audio, "Volume"};
  add(slider(at, "masterVolume", "Master Volume", "Scales every sound the game plays.", 100, 0,
             100, 1, "%"));
  add(slider(at, "sfxVolume", "Effects Volume",
             "Volume of sound effects other than weapons.", 100, 0, 100, 1, "%"));
  add(slider(at, "weaponVolume", "Weapon Volume", "Volume of weapon sounds.", 100, 0, 100, 1,
             "%"));
  at.name = "Playback";
  add(toggle(at, "playSFX", "Sound Effects", "Plays sound effects."));
  add(toggle(at, "playPewPew", "Weapon Sounds", "Plays weapon sounds."));
  // Parameters.setDefaults entries that have no row.
  add(row({C::keybinds, {}}, K::hidden_key, "escapeToNexus2", {}, {}, 116));
  return options;
}

const std::vector<OptionDef>& table() {
  static const auto value = build();
  return value;
}

// KeyCodes.CharCodeStrings.
constexpr std::pair<int, std::string_view> key_names[]{{0, "[Unset]"},    {8, "Backspace"},
                                                       {9, "Tab"},        {12, "Clear"},
                                                       {13, "Enter"},     {15, "Cmd"},
                                                       {16, "Shift"},     {17, "Ctrl"},
                                                       {18, "Alt"},       {19, "Pause"},
                                                       {20, "CapsLock"},  {27, "Esc"},
                                                       {32, "Space"},     {33, "PgUp"},
                                                       {34, "PgDn"},      {35, "End"},
                                                       {36, "Home"},      {37, "Left"},
                                                       {38, "Up"},        {39, "Right"},
                                                       {40, "Down"},      {45, "Insert"},
                                                       {46, "Delete"},    {48, "0"},
                                                       {49, "1"},         {50, "2"},
                                                       {51, "3"},         {52, "4"},
                                                       {53, "5"},         {54, "6"},
                                                       {55, "7"},         {56, "8"},
                                                       {57, "9"},         {65, "A"},
                                                       {66, "B"},         {67, "C"},
                                                       {68, "D"},         {69, "E"},
                                                       {70, "F"},         {71, "G"},
                                                       {72, "H"},         {73, "I"},
                                                       {74, "J"},         {75, "K"},
                                                       {76, "L"},         {77, "M"},
                                                       {78, "N"},         {79, "O"},
                                                       {80, "P"},         {81, "Q"},
                                                       {82, "R"},         {83, "S"},
                                                       {84, "T"},         {85, "U"},
                                                       {86, "V"},         {87, "W"},
                                                       {88, "X"},         {89, "Y"},
                                                       {90, "Z"},         {92, "Win"},
                                                       {93, "Menu"},      {96, "Numpad 0"},
                                                       {97, "Numpad 1"},  {98, "Numpad 2"},
                                                       {99, "Numpad 3"},  {100, "Numpad 4"},
                                                       {101, "Numpad 5"}, {102, "Numpad 6"},
                                                       {103, "Numpad 7"}, {104, "Numpad 8"},
                                                       {105, "Numpad 9"}, {106, "Numpad *"},
                                                       {107, "Numpad +"}, {108, "Numpad Enter"},
                                                       {109, "Numpad -"}, {110, "Numpad ."},
                                                       {111, "Numpad /"}, {112, "F1"},
                                                       {113, "F2"},       {114, "F3"},
                                                       {115, "F4"},       {116, "F5"},
                                                       {117, "F6"},       {118, "F7"},
                                                       {119, "F8"},       {120, "F9"},
                                                       {121, "F10"},      {122, "F11"},
                                                       {123, "F12"},      {124, "F13"},
                                                       {125, "F14"},      {126, "F15"},
                                                       {144, "NumLock"},  {145, "ScrLock"},
                                                       {186, ";"},        {187, "="},
                                                       {188, ","},        {189, "-"},
                                                       {190, "."},        {191, "/"},
                                                       {192, "`"},        {219, "["},
                                                       {220, "\\"},       {221, "]"},
                                                       {222, "'"}};
constexpr double default_angle = 7 * std::numbers::pi / 4;
// The Resolution choice's sizes. Values are 1-based indices into this table, so new sizes are only
// appended. build() sets the order the dropdown lists them in.
constexpr std::array<ResolutionDef, 20> resolution_table{
    {{800, 600, aspect_4_3},     {1024, 768, aspect_4_3},    {1280, 960, aspect_4_3},
     {1600, 1200, aspect_4_3},   {1280, 800, aspect_16_10},  {1440, 900, aspect_16_10},
     {1680, 1050, aspect_16_10}, {1920, 1200, aspect_16_10}, {2560, 1600, aspect_16_10},
     {1280, 720, aspect_16_9},   {1366, 768, aspect_16_9},   {1600, 900, aspect_16_9},
     {1920, 1080, aspect_16_9},  {2560, 1440, aspect_16_9},  {3840, 2160, aspect_16_9},
     {2560, 1080, aspect_21_9},  {3440, 1440, aspect_21_9},  {1440, 1080, aspect_4_3},
     {1720, 720, aspect_21_9},   {1920, 810, aspect_21_9}}};
}

std::string_view options_category_name(OptionsCategory category) {
  constexpr std::array<std::string_view, options_category_count> names{"Keybinds", "Gameplay",
                                                                       "Video", "Audio"};
  return names.at(static_cast<std::size_t>(category));
}

std::span<const OptionDef> option_defs() {
  return table();
}

std::vector<const OptionDef*> category_options(OptionsCategory category) {
  std::vector<const OptionDef*> result;
  for (const auto& option : table()) {
    if (option.category == category && option.kind != K::hidden_key &&
        option.kind != K::hidden_value) {
      result.push_back(&option);
    }
  }
  return result;
}

std::vector<std::string_view> category_sections(OptionsCategory category) {
  std::vector<std::string_view> result;
  for (const auto* option : category_options(category)) {
    if (std::find(result.begin(), result.end(), option->section) == result.end()) {
      result.push_back(option->section);
    }
  }
  return result;
}

const OptionDef* find_option(std::string_view param) {
  for (const auto& option : table()) {
    if (!option.param.empty() && option.param == param) {
      return &option;
    }
  }
  return nullptr;
}

std::string_view key_name(int code) {
  for (const auto& [value, name] : key_names) {
    if (value == code) {
      return name;
    }
  }
  return {};
}

int slider_end(const OptionDef& option) {
  return option.top_label.empty() ? option.max : option.max + option.step;
}

int slider_position(const OptionDef& option, int value) {
  if (!option.top_label.empty() && value == 0) {
    return slider_end(option);
  }
  return std::clamp(value, option.min, option.max);
}

int slider_value(const OptionDef& option, int position) {
  if (!option.top_label.empty() && position > option.max) {
    return 0;
  }
  return std::clamp(position, option.min, option.max);
}

std::string value_text(const OptionDef& option, int value) {
  switch (option.kind) {
    case K::toggle:
      return value ? "On" : "Off";
    case K::choice:
      for (const auto& [label, choice] : option.choices) {
        if (choice == value) {
          return std::string(label);
        }
      }
      return {};
    case K::slider:
      if (!option.top_label.empty() && value == 0) {
        return std::string(option.top_label);
      }
      return std::to_string(value) + std::string(option.unit);
    case K::key:
    case K::hidden_key:
      return std::string(key_name(value));
    case K::hidden_value:
      return std::to_string(value);
    case K::checks:
      // A checks row shows its boxes, not one value.
      return {};
  }
  return {};
}

std::span<const ResolutionDef> resolutions() {
  return resolution_table;
}

const ResolutionDef* find_resolution(int value) {
  if (value < 1 || value > static_cast<int>(resolution_table.size())) {
    return nullptr;
  }
  return &resolution_table[static_cast<std::size_t>(value - 1)];
}

bool resolution_offered(int value, int aspect, int max_width, int max_height) {
  if (value == 0) {
    return true;
  }
  const auto* size = find_resolution(value);
  return size && (aspect == aspect_any || size->aspect == aspect) &&
         (max_width <= 0 || size->width <= max_width) &&
         (max_height <= 0 || size->height <= max_height);
}

int largest_resolution(int aspect, int max_width, int max_height) {
  int best = 0;
  int best_area = 0;
  for (std::size_t i = 0; i < resolution_table.size(); ++i) {
    const int value = static_cast<int>(i) + 1;
    const auto& size = resolution_table[i];
    if (resolution_offered(value, aspect, max_width, max_height) &&
        size.width * size.height > best_area) {
      best = value;
      best_area = size.width * size.height;
    }
  }
  return best;
}

Settings::Settings() : camera_angle_(default_angle), camera_zoom_(1) {
  for (const auto& option : table()) {
    // A checks row's param is only its id, so it is not a stored value.
    if (!option.param.empty() && option.kind != K::checks) {
      values_.emplace(std::string(option.param), option.fallback);
    }
  }
}

int Settings::get(std::string_view param) const {
  const auto found = values_.find(param);
  if (found != values_.end()) {
    return found->second;
  }
  const auto* option = find_option(param);
  return option ? option->fallback : 0;
}

void Settings::set_key(std::string_view param, int code) {
  const auto* option = find_option(param);
  if (!option || (option->kind != K::key && option->kind != K::hidden_key) || code < 0 ||
      code > 255) {
    return;
  }
  // Parameters.setKey also unsets the binding being replaced on any other action. An unset
  // code (0) is not a key and clears nothing else.
  if (code != key_unset) {
    for (auto& [name, value] : values_) {
      if (value == code) {
        const auto* other = find_option(name);
        if (other && (other->kind == K::key || other->kind == K::hidden_key)) {
          value = key_unset;
        }
      }
    }
  }
  values_[std::string(param)] = code;
}

bool Settings::valid(const OptionDef& option, int value) {
  switch (option.kind) {
    case K::key:
    case K::hidden_key:
      return value >= 0 && value <= 255;
    case K::hidden_value:
    case K::toggle:
      return value == 0 || value == 1;
    case K::choice:
      return std::any_of(option.choices.begin(), option.choices.end(),
                         [&](const auto& choice) { return choice.second == value; });
    case K::slider:
      return (value >= option.min && value <= option.max) ||
             (!option.top_label.empty() && value == 0);
    case K::checks:
      return false;
  }
  return false;
}

bool Settings::set_value(std::string_view param, int value) {
  const auto* option = find_option(param);
  if (!option ||
      (option->kind != K::toggle && option->kind != K::hidden_value && option->kind != K::choice &&
       option->kind != K::slider) ||
      !valid(*option, value)) {
    return false;
  }
  auto& slot = values_[std::string(param)];
  if (slot == value) {
    return false;
  }
  slot = value;
  // Options.onDefaultCameraAngleChange.
  if (param == "defaultCameraAngle") {
    camera_angle_ = default_camera_angle();
  }
  return true;
}

double Settings::default_camera_angle() const {
  return get("defaultCameraAngle") != 0 ? default_angle : 0;
}

void Settings::reset_category(OptionsCategory category) {
  // Hidden toggles reset with their category (Performance Stats and the checks rows' boxes).
  for (const auto& option : table()) {
    if (option.category == category && !option.param.empty() && option.kind != K::hidden_key &&
        option.kind != K::checks) {
      values_[std::string(option.param)] = option.fallback;
    }
  }
}

void Settings::reset_all() {
  // Parameters.cameraZoom is not an option row, so a reset keeps it.
  const auto zoom = camera_zoom_;
  *this = Settings();
  camera_zoom_ = zoom;
}

Preferences Settings::preferences() const {
  Preferences p;
  p.hp_bars = flag("hpBars");
  p.ui_quality = get("uiQuality") == 0 ? UiQuality::low : UiQuality::high;
  // Particles: 0 is Off, 2 Minimal and 1 All, so a saved Off or All keeps its meaning.
  const int particles = get("particles");
  p.particles = particles == 0   ? ParticleQuality::off
                : particles == 2 ? ParticleQuality::minimal
                                 : ParticleQuality::all;
  // Shadows: 0 is Off, 2 Minimal and 1 High, so a saved Off or High keeps its meaning.
  const int shadows = get("drawShadows");
  p.shadows = shadows == 0   ? ShadowQuality::off
              : shadows == 2 ? ShadowQuality::minimal
                             : ShadowQuality::high;
  p.proj_outline = flag("projOutline");
  p.smart_projectiles = flag("smartProjectiles");
  p.text_bubbles = flag("textBubbles");
  p.bar_text = flag("toggleBarText");
  p.screen_shake = flag("screenShake");
  p.ally_shots = flag("allyShots");
  p.ally_damage = flag("allyDamage");
  p.ally_notifs = flag("allyNotifs");
  // Allies: 0 is Off, 1 All and 2 Minimal, so a saved Off or All keeps its meaning.
  const int ally_particles = get("allyParticles");
  p.ally_particles = ally_particles == 0   ? AllyParticles::off
                     : ally_particles == 2 ? AllyParticles::minimal
                                           : AllyParticles::all;
  p.show_tier_tag = flag("showTierTag");
  p.show_quest_portraits = flag("showQuestPortraits");
  p.show_guild_invite_popup = flag("showGuildInvitePopup");
  p.show_trade_popup = flag("showTradePopup");
  p.fade_players = flag("fadePlayers");
  p.fade_guild = flag("fadeGuild");
  p.fade_locked = flag("fadeLocked");
  p.play_sfx = flag("playSFX");
  p.play_pew_pew = flag("playPewPew");
  p.master_volume = static_cast<float>(get("masterVolume")) / 100.F;
  p.sfx_volume = static_cast<float>(get("sfxVolume")) / 100.F;
  p.weapon_volume = static_cast<float>(get("weaponVolume")) / 100.F;
  p.player_alpha = static_cast<float>(get("playerAlpha")) / 100.F;
  return p;
}

float player_alpha(const Preferences& prefs, bool is_local, bool locked, bool same_guild) {
  if (is_local) {
    return 1;
  }
  if (locked) {
    return prefs.fade_locked ? prefs.player_alpha : 1;
  }
  if (same_guild) {
    return prefs.fade_guild ? prefs.player_alpha : 1;
  }
  return prefs.fade_players ? prefs.player_alpha : 1;
}

double step_camera_zoom(double zoom, double steps) {
  const double current = std::isfinite(zoom) ? zoom : 1;
  const double notches = std::isfinite(steps) ? steps : 0;
  // Tenths keep repeated notches on exact values such as 1.1 and 2.
  const double tenths = std::round(current * 10 + notches);
  return std::clamp(tenths, min_camera_zoom * 10, max_camera_zoom * 10) / 10;
}

Settings Settings::load(const std::filesystem::path& path) {
  Settings settings;
  std::ifstream file(path);
  if (!file) {
    return settings;
  }
  try {
    const auto json = nlohmann::json::parse(file);
    if (!json.is_object()) {
      return settings;
    }
    if (const auto values = json.find("values"); values != json.end() && values->is_object()) {
      for (const auto& [name, value] : values->items()) {
        const auto* option = find_option(name);
        if (!option || !value.is_number_integer()) {
          continue;
        }
        const auto number = value.get<std::int64_t>();
        if (number < -1000 || number > 1000000) {
          continue;
        }
        if (valid(*option, static_cast<int>(number))) {
          settings.values_[name] = static_cast<int>(number);
        }
      }
    }
    if (const auto angle = json.find("cameraAngle"); angle != json.end() && angle->is_number()) {
      const auto value = angle->get<double>();
      if (std::isfinite(value) && std::fabs(value) < 1000) {
        settings.camera_angle_ = value;
      }
    } else {
      settings.camera_angle_ = settings.default_camera_angle();
    }
    // A missing, non-numeric or out-of-range zoom keeps the default of 1.
    if (const auto zoom = json.find("cameraZoom"); zoom != json.end() && zoom->is_number()) {
      const auto value = zoom->get<double>();
      if (std::isfinite(value) && value >= min_camera_zoom && value <= max_camera_zoom) {
        settings.camera_zoom_ = step_camera_zoom(value, 0);
      }
    }
  } catch (const std::exception&) {
    return Settings();
  }
  return settings;
}

bool Settings::save(const std::filesystem::path& path) const {
  nlohmann::json json{{"version", 1},
                      {"values", nlohmann::json::object()},
                      {"cameraAngle", camera_angle_},
                      {"cameraZoom", camera_zoom_}};
  for (const auto& [name, value] : values_) {
    json["values"][name] = value;
  }
  std::error_code error;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), error);
  }
  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream file(temporary, std::ios::trunc);
    if (!file) {
      return false;
    }
    file << json.dump(2) << '\n';
    if (!file) {
      return false;
    }
  }
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return false;
  }
  return true;
}

std::filesystem::path Settings::path_beside(const std::filesystem::path& account_file) {
  if (account_file.empty()) {
    return account_path().parent_path() / "settings.json";
  }
  return account_file.parent_path() / (account_file.stem().string() + ".settings.json");
}
}
