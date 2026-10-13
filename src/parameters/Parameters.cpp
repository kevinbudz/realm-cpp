#include "realm/parameters/Parameters.hpp"
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
using T = OptionsTab;
constexpr int a_ = 65, d_ = 68, e_ = 69, q_ = 81, s_ = 83, w_ = 87;
OptionDef key(T tab, std::string_view param, std::string_view label, std::string_view tip, int code,
              std::string_view enabled_by = {}) {
  return {tab, K::key, param, label, tip, code, {}, enabled_by};
}
OptionDef toggle(T tab, std::string_view param, std::string_view label, std::string_view tip, bool value = true) {
  return {tab, K::choice, param, label, tip, value ? 1 : 0, {{"On", 1}, {"Off", 0}}, {}};
}
OptionDef choice(T tab, std::string_view param, std::string_view label, std::string_view tip, int value,
                 std::vector<std::pair<std::string_view, int>> choices) {
  return {tab, K::choice, param, label, tip, value, std::move(choices), {}};
}
OptionDef spacer(T tab) { return {tab, K::spacer, {}, {}, {}, 0, {}, {}}; }
std::vector<OptionDef> build() {
  std::vector<OptionDef> r;
  const auto c = T::controls, g = T::gameplay, h = T::hotkeys, t = T::chat, v = T::graphics, s = T::sound;
  // Options.addControlsOptions
  r.push_back(key(c, "moveUp", "Move Up", "Key to will move character up", w_));
  r.push_back(key(c, "moveLeft", "Move Left", "Key to will move character to the left", a_));
  r.push_back(key(c, "moveDown", "Move Down", "Key to will move character down", s_));
  r.push_back(key(c, "moveRight", "Move Right", "Key to will move character to the right", d_));
  r.push_back(choice(c, "allowRotation", "Allow Camera Rotation", "Toggles whether to allow for camera rotation", 0,
                     {{"On", 1}, {"Off", 0}}));
  r.push_back(spacer(c));
  r.push_back(key(c, "rotateLeft", "Rotate Left", "Key to will rotate the camera to the left", q_, "allowRotation"));
  r.push_back(key(c, "rotateRight", "Rotate Right", "Key to will rotate the camera to the right", e_, "allowRotation"));
  r.push_back(key(c, "useSpecial", "Use Special Ability", "This key will activate your special ability", 32));
  r.push_back(key(c, "autofireToggle", "Autofire Toggle", "This key will toggle autofire", 73));
  r.push_back(key(c, "resetToDefaultCameraAngle", "Reset To Default Camera Angle",
                  "This key will reset the camera angle to the default position", 82));
  r.push_back(key(c, "togglePerformanceStats", "Toggle Performance Stats",
                  "This key will toggle a display of fps and memory usage", 0));
  r.push_back(key(c, "toggleCentering", "Toggle Centering of Player",
                  "This key will toggle the position between centered and offset", 0));
  r.push_back(key(c, "interact", "Interact/Buy", "This key will allow you to enter a portal or buy an item", 48));
  // Options.addHotKeysOptions
  r.push_back(key(h, "useHealthPotion", "Use Health Potion", "This key will use health potions if available", 70));
  r.push_back(key(h, "useMagicPotion", "Use Magic Potion", "This key will use magic potions if available", 86));
  static const std::array<std::string, 8> slot_params{"useEquipInvSlot1", "useEquipInvSlot2", "useEquipInvSlot3",
                                                      "useEquipInvSlot4", "useEquipInvSlot5", "useEquipInvSlot6",
                                                      "useEquipInvSlot7", "useEquipInvSlot8"};
  static const std::array<std::string, 8> slot_labels{"Use/Equip Inventory Slot 1", "Use/Equip Inventory Slot 2",
                                                      "Use/Equip Inventory Slot 3", "Use/Equip Inventory Slot 4",
                                                      "Use/Equip Inventory Slot 5", "Use/Equip Inventory Slot 6",
                                                      "Use/Equip Inventory Slot 7", "Use/Equip Inventory Slot 8"};
  static const std::array<std::string, 8> slot_tips{
      "Use/Equip item in inventory slot 1", "Use/Equip item in inventory slot 2", "Use/Equip item in inventory slot 3",
      "Use/Equip item in inventory slot 4", "Use/Equip item in inventory slot 5", "Use/Equip item in inventory slot 6",
      "Use/Equip item in inventory slot 7", "Use/Equip item in inventory slot 8"};
  for (std::size_t i = 0; i < 8; ++i)
    r.push_back(key(h, slot_params[i], slot_labels[i], slot_tips[i], 49 + static_cast<int>(i)));
  r.push_back(key(h, "miniMapZoomIn", "Mini-Map Zoom In", "This key will zoom in the minimap", 187));
  r.push_back(key(h, "miniMapZoomOut", "Mini-Map Zoom Out", "This key will zoom out the minimap", 189));
  r.push_back(key(h, "escapeToNexus", "Escape To Nexus", "This key will instantly escape you to the Nexus", 45));
  r.push_back(key(h, "options", "Show Options", "This key will bring up the options screen", 79));
  r.push_back(key(h, "switchTabs", "Switch Tabs", "This key will switch from available tabs", 66));
  r.push_back(key(h, "toggleFullscreenMode", "Toggle Fullscreen", "This toggles whether to go fullscreen or not", 0));
  // Options.addChatOptions
  r.push_back(key(t, "chat", "Activate Chat", "This key will bring up the chat input box", 13));
  r.push_back(key(
      t, "chatCommand", "Start Chat Command",
      "This key will bring up the chat with a '/' prepended to allow for commands such as /who, /ignore, etc.", 191));
  r.push_back(key(t, "tell", "Begin Tell", "This key will bring up a tell (private message) in the chat input box", 9));
  r.push_back(key(t, "guildChat", "Begin Guild Chat", "This key will bring up a guild chat in the chat input box", 71));
  r.push_back(
      key(t, "scrollChatUp", "Scroll Chat Up", "This key will scroll up to older messages in the chat buffer", 33));
  r.push_back(key(t, "scrollChatDown", "Scroll Chat Down",
                  "This key will scroll down to newer messages in the chat buffer", 34));
  // Options.addGameplayOptions
  r.push_back(choice(g, "defaultCameraAngle", "Default Camera Angle", "This toggles the default camera angle", 1,
                     {{"45\xC2\xB0", 1}, {"0\xC2\xB0", 0}}));
  r.push_back(toggle(g, "centerOnPlayer", "Center On Player", "This toggles whether the player is centered or offset"));
  r.push_back(
      toggle(g, "showQuestPortraits", "Show Quest Portraits", "This toggles whether quest portraits are displayed"));
  r.push_back(toggle(g, "textBubbles", "Draw Text Bubbles", "This toggles whether to draw text bubbles"));
  r.push_back(toggle(g, "showGuildInvitePopup", "Show Guild Invite Panel",
                     "This toggles whether to show guild invites in the lower-right panel or just in chat."));
  r.push_back(toggle(g, "hpBars", "Health Bars", "Enabling this will render health bars below entities."));
  r.push_back(toggle(g, "showTierTag", "Show Tier Tag", "This toggles whether to show tier tags on your gear."));
  r.push_back(toggle(g, "projOutline", "Projectile Outline", "Makes projectiles render with an outline."));
  std::vector<std::pair<std::string_view, int>> cursors{{"OS", -1}};
  static const std::array<std::string, 16> cursor_labels{"0", "1", "2",  "3",  "4",  "5",  "6",  "7",
                                                         "8", "9", "10", "11", "12", "13", "14", "15"};
  for (int i = 0; i < 16; ++i) cursors.emplace_back(cursor_labels[static_cast<std::size_t>(i)], i);
  r.push_back(choice(g, "cursor", "Cursor", "Changing this will give you a new mouse cursor.", 4, std::move(cursors)));
  // Options.addGraphicsOptions. Hardware Acceleration is shown only when the GPU path can
  // fail over; the native renderer is always the GPU path.
  r.push_back(toggle(v, "drawShadows", "Draw Shadows", "This toggles whether to draw shadows"));
  r.push_back(
      toggle(v, "particles", "Particles",
             "If enabled, particles which are not necessary for gameplay are rendered (e.g. hit/death particles)."));
  r.push_back(toggle(v, "allyShots", "Ally Shots",
                     "This toggles whether to show and render ally shots. Disable this to improve performance."));
  r.push_back(
      toggle(v, "allyDamage", "Ally Damage",
             "This toggles whether to show damage dealt to and by allies. Disable this to improve performance."));
  r.push_back(toggle(
      v, "allyNotifs", "Ally Notifications",
      "This toggles whether to show notifications targeted at other players. Disable this to improve performance."));
  r.push_back(choice(v, "quality", "Quality", "Enabling this will render UI elements at higher/lower quality.", 1,
                     {{"High", 1}, {"Low", 0}}));
  r.push_back(
      toggle(v, "vsync", "VSync", "Synchronizes rendering with the display refresh to prevent screen tearing."));
  r.push_back(
      choice(v, "maxFPS", "Max FPS",
             "Caps the frame rate. Unlimited renders as fast as possible (up to your display refresh with VSync on).",
             60, {{"30", 30}, {"60", 60}, {"120", 120}, {"144", 144}, {"165", 165}, {"240", 240}, {"Unlimited", 0}}));
  r.push_back(
      toggle(v, "fullscreenMode", "Fullscreen Mode", "This toggles whether the game is fullscreen or not", false));
  // Options.addSoundOptions
  r.push_back(toggle(s, "playSFX", "Play Sound Effects", "This toggles whether sound effects are played"));
  r.push_back(spacer(s));
  r.push_back(toggle(s, "playPewPew", "Play Weapon Sounds", "This toggles whether weapon sounds are played"));
  // Parameters.setDefaults entries that have no row.
  r.push_back({T::hotkeys, K::hidden_key, "escapeToNexus2", {}, {}, 116, {}, {}});
  r.push_back({T::gameplay, K::hidden_value, "showTradePopup", {}, {}, 1, {}, {}});
  return r;
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
}
std::string_view options_tab_name(OptionsTab tab) {
  constexpr std::array<std::string_view, options_tab_count> names{"Controls", "Gameplay", "Hot Keys",
                                                                  "Chat",     "Graphics", "Sound"};
  return names.at(static_cast<std::size_t>(tab));
}
std::span<const OptionDef> option_defs() { return table(); }
std::vector<const OptionDef*> tab_options(OptionsTab tab) {
  std::vector<const OptionDef*> result;
  for (const auto& option : table())
    if (option.tab == tab && option.kind != K::hidden_key && option.kind != K::hidden_value) result.push_back(&option);
  return result;
}
const OptionDef* find_option(std::string_view param) {
  for (const auto& option : table())
    if (!option.param.empty() && option.param == param) return &option;
  return nullptr;
}
std::string_view key_name(int code) {
  for (const auto& [value, name] : key_names)
    if (value == code) return name;
  return {};
}
Settings::Settings() : camera_angle_(default_angle) {
  for (const auto& option : table())
    if (!option.param.empty()) values_.emplace(std::string(option.param), option.fallback);
}
int Settings::get(std::string_view param) const {
  const auto found = values_.find(param);
  if (found != values_.end()) return found->second;
  const auto* option = find_option(param);
  return option ? option->fallback : 0;
}
void Settings::set_key(std::string_view param, int code) {
  const auto* option = find_option(param);
  if (!option || (option->kind != K::key && option->kind != K::hidden_key) || code < 0 || code > 255) return;
  // Parameters.setKey also unsets the binding being replaced on any other action. An unset
  // code (0) is not a key and clears nothing else.
  if (code != key_unset)
    for (auto& [name, value] : values_)
      if (value == code) {
        const auto* other = find_option(name);
        if (other && (other->kind == K::key || other->kind == K::hidden_key)) value = key_unset;
      }
  values_[std::string(param)] = code;
}
bool Settings::valid(const OptionDef& option, int value) {
  switch (option.kind) {
    case K::key:
    case K::hidden_key:
      return value >= 0 && value <= 255;
    case K::hidden_value:
      return value == 0 || value == 1;
    case K::choice:
      return std::any_of(option.choices.begin(), option.choices.end(),
                         [&](const auto& c) { return c.second == value; });
    case K::spacer:
      return false;
  }
  return false;
}
bool Settings::set_choice(std::string_view param, int value) {
  const auto* option = find_option(param);
  if (!option || option->kind != K::choice || !valid(*option, value)) return false;
  auto& slot = values_[std::string(param)];
  if (slot == value) return false;
  slot = value;
  // Options.onDefaultCameraAngleChange.
  if (param == "defaultCameraAngle") camera_angle_ = default_camera_angle();
  return true;
}
double Settings::default_camera_angle() const { return get("defaultCameraAngle") != 0 ? default_angle : 0; }
void Settings::reset_tab(OptionsTab tab) {
  for (const auto& option : table())
    if (option.tab == tab && !option.param.empty() && option.kind != K::hidden_key && option.kind != K::hidden_value)
      values_[std::string(option.param)] = option.fallback;
}
void Settings::reset_all() { *this = Settings(); }
Preferences Settings::preferences() const {
  Preferences p;
  p.hp_bars = flag("hpBars");
  p.particles = flag("particles");
  p.draw_shadows = flag("drawShadows");
  p.proj_outline = flag("projOutline");
  p.text_bubbles = flag("textBubbles");
  p.ally_shots = flag("allyShots");
  p.ally_damage = flag("allyDamage");
  p.ally_notifs = flag("allyNotifs");
  p.show_tier_tag = flag("showTierTag");
  p.show_quest_portraits = flag("showQuestPortraits");
  p.show_guild_invite_popup = flag("showGuildInvitePopup");
  p.show_trade_popup = flag("showTradePopup");
  p.play_sfx = flag("playSFX");
  p.play_pew_pew = flag("playPewPew");
  return p;
}
Settings Settings::load(const std::filesystem::path& path) {
  Settings settings;
  std::ifstream file(path);
  if (!file) return settings;
  try {
    const auto json = nlohmann::json::parse(file);
    if (!json.is_object()) return settings;
    if (const auto values = json.find("values"); values != json.end() && values->is_object())
      for (const auto& [name, value] : values->items()) {
        const auto* option = find_option(name);
        if (!option || !value.is_number_integer()) continue;
        const auto number = value.get<std::int64_t>();
        if (number < -1000 || number > 1000000) continue;
        if (valid(*option, static_cast<int>(number))) settings.values_[name] = static_cast<int>(number);
      }
    if (const auto angle = json.find("cameraAngle"); angle != json.end() && angle->is_number()) {
      const auto value = angle->get<double>();
      if (std::isfinite(value) && std::fabs(value) < 1000) settings.camera_angle_ = value;
    } else
      settings.camera_angle_ = settings.default_camera_angle();
  } catch (const std::exception&) {
    return Settings();
  }
  return settings;
}
bool Settings::save(const std::filesystem::path& path) const {
  nlohmann::json json{{"version", 1}, {"values", nlohmann::json::object()}, {"cameraAngle", camera_angle_}};
  for (const auto& [name, value] : values_) json["values"][name] = value;
  std::error_code error;
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream file(temporary, std::ios::trunc);
    if (!file) return false;
    file << json.dump(2) << '\n';
    if (!file) return false;
  }
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return false;
  }
  return true;
}
std::filesystem::path Settings::path_beside(const std::filesystem::path& account_file) {
  if (account_file.empty()) return account_path().parent_path() / "settings.json";
  return account_file.parent_path() / (account_file.stem().string() + ".settings.json");
}
}
