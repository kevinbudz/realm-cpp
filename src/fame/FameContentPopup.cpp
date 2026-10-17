#include "realm/fame/FameContentPopup.hpp"
#include "realm/appengine/CharacterFameStats.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/objects/Catalog.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <future>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace realm {
namespace {
std::string escaped(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const char c : value) {
    if (c == '&') {
      result += "&amp;";
    } else if (c == '<') {
      result += "&lt;";
    } else if (c == '>') {
      result += "&gt;";
    } else if (c == '"') {
      result += "&quot;";
    } else {
      result += c;
    }
  }
  return result;
}

std::string dp(double value) {
  return std::to_string(std::lround(value)) + "dp";
}

// The element (or nearest ancestor) carrying an attribute: text can be the event target.
Rml::Element* holder(Rml::Element* element, const char* attribute) {
  while (element && !element->HasAttribute(attribute)) {
    element = element->GetParentNode();
  }
  return element;
}

// DateFormatter "MMMM DD, YYYY"; today when the server has no creation time (Flash's fallback).
std::string created_date(std::int64_t stamp) {
  const auto time = stamp > 0 ? static_cast<std::time_t>(stamp) : std::time(nullptr);
  std::tm date{};
#ifdef _WIN32
  const bool valid = localtime_s(&date, &time) == 0;
#else
  const bool valid = localtime_r(&time, &date) != nullptr;
#endif
  if (!valid) {
    return "Unknown";
  }
  constexpr const char* months[]{"January",   "February", "March",    "April",
                                 "May",       "June",     "July",     "August",
                                 "September", "October",  "November", "December"};
  char day[3];
  std::snprintf(day, sizeof day, "%02d", date.tm_mday);
  return std::string(months[date.tm_mon]) + " " + day + ", " + std::to_string(date.tm_year + 1900);
}

// FameContentPopupMediator's Statistics lines: label, <FameStats> tag and tooltip. Accuracy and
// Dungeons Completed are derived.
struct StatLine {
  const char* label;
  const char* tag;
  const char* tooltip;
};

constexpr StatLine stat_lines[]{
    {"Shots Fired", "Shots", "The total number of shots fired by this character."},
    {"Shots Hit", "ShotsThatDamage",
     "The total number of enemy hitting shots fired by this character."},
    {"Potions Drunk", "PotionsDrank", "The number of potions this character has consumed."},
    {"Abilities Used", "AbilitiesUsed", "The number of times this character used their abilities."},
    {"Teleported", "Teleports", "The number of times this character has teleported."},
    {"Dungeons Completed", nullptr, "The number of dungeons completed by this character."},
    {"Monster Kills", "MonsterKills", "Total number of monsters killed by this character."},
    {"God Kills", "GodKills", "Total number of Gods killed by this character."},
    {"Oryx Kills", "OryxKills", "Total number of Oryx kills for this character."},
    {"Monster Assists", "MonsterAssists",
     "Total number of monster kills assisted by this character."},
    {"God Assists", "GodAssists", "Total number of God kills assisted by this character."},
    {"Party Level Ups", "LevelUpAssists", "Total number of level ups assisted by this character."},
    {"Quests Completed", "QuestsCompleted", "Total number of quests completed by this character."},
    {"Cube Kills", "CubeKills", "Total number of Cube Enemies killed by this character."},
    {"Accuracy", nullptr, nullptr},
    {"Tiles Seen", "TilesUncovered", nullptr},
    {"Minutes Active", "MinutesActive", "Time spent actively defeating Oryx's minions."},
};

// ObjectLibrary.dungeonToPortalTextureData_: the portal whose <DungeonName> is the dungeon, by the
// label the server sends.
std::string portal_id(std::string_view label) {
  if (label == "Sprite World") {
    return "Glowing Portal";
  }
  if (label == "Cave of a Thousand Treasures") {
    return "Treasure Cave Portal";
  }
  if (label == "Davy Jones' Locker") {
    return "Davy Jones's Locker Portal";
  }
  return std::string(label) + " Portal";
}

struct Tip {
  std::string title;
  std::string body;
};
}

struct FameContentPopup::Impl {
  Impl(ui::RmlHost& ui_host, const Catalog& objects, const HudArt& hud_art)
      : host(ui_host), catalog(objects), art(hud_art) {
    document = host.load("fame");
    if (!document) {
      throw std::runtime_error("The RmlUi fame document did not load");
    }
    for (const char* id : {"portrait", "name", "level", "created", "base-value", "lines", "status",
                           "death-value", "tooltip", "tip-title", "tip-body", "tabs"}) {
      if (!document->GetElementById(id)) {
        throw std::runtime_error("The RmlUi fame document is missing its parts");
      }
    }
    lines = document->GetElementById("lines");
    tooltip = document->GetElementById("tooltip");
    host.listen(*document, "click", [this](Rml::Event& event) { on_click(event); }, true);
    host.listen(*document, "mousemove", [this](Rml::Event& event) { on_hover(event); }, true);
  }

  ~Impl() {
    drain();
    host.close(document);
  }

  // A request still running when the popup goes away is waited for: its future would block in its
  // destructor anyway, and http_post has its own timeout.
  void drain() {
    if (request.valid()) {
      request.wait();
    }
  }

  void open(std::string address, std::uint16_t port, net::FormFields fields, int character_id) {
    ++generation;
    visible_ = true;
    stats.reset();
    tab = 0;
    set_status("Loading...");
    fields.emplace_back("charId", std::to_string(character_id));
    if (request.valid()) {
      // One request at a time; the newest open wins once the old reply arrives.
      pending = Pending{std::move(address), port, std::move(fields), generation};
      return;
    }
    start(std::move(address), port, std::move(fields), generation);
  }

  void start(std::string address, std::uint16_t port, net::FormFields fields, std::uint64_t id) {
    request_generation = id;
    request = std::async(std::launch::async,
                         [address = std::move(address), port, fields = std::move(fields)] {
                           return net::http_post(address, port, "/char/fameStats", fields);
                         });
  }

  void poll() {
    if (!request.valid() ||
        request.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      return;
    }
    std::optional<CharacterFameStats> result;
    std::string failure;
    try {
      result = parse_character_fame_stats(request.get());
    } catch (const std::exception& e) {
      failure = e.what();
    }
    if (pending) {
      auto next = std::move(*pending);
      pending.reset();
      start(std::move(next.address), next.port, std::move(next.fields), next.generation);
      return;
    }
    if (request_generation != generation) {
      return;
    }
    if (result) {
      stats = std::move(result);
      show_stats();
    } else {
      set_status(failure.empty() ? "Could not load the fame overview." : failure);
    }
  }

  void set_status(const std::string& text) {
    document->GetElementById("status")->SetInnerRML(escaped(text));
    for (const char* id : {"name", "level", "created", "base-value", "death-value"}) {
      document->GetElementById(id)->SetInnerRML("");
    }
    document->GetElementById("portrait")->SetProperty("display", "none");
    lines->SetInnerRML("");
    tips.clear();
    hide_tip();
    sync_tabs();
  }

  void show_stats() {
    const auto& s = *stats;
    document->GetElementById("status")->SetInnerRML("");
    document->GetElementById("name")->SetInnerRML(escaped(s.name));
    const auto* object = catalog.object(static_cast<std::uint16_t>(s.character.object_type));
    const std::string class_name =
        object ? (object->display_id.empty() ? object->id : object->display_id) : "";
    document->GetElementById("level")->SetInnerRML(
        escaped("Level " + std::to_string(s.character.level) + ", " + class_name));
    document->GetElementById("created")->SetInnerRML(
        escaped("Created on " + created_date(s.created_on)));
    document->GetElementById("base-value")->SetInnerRML(std::to_string(s.base_fame));
    document->GetElementById("death-value")->SetInnerRML(std::to_string(s.total_fame));

    // Player.getFamePortrait(200), centred in the 69 x 80 popup_content_decoration at (0, 5).
    auto* element = document->GetElementById("portrait");
    try {
      portrait = art.player_fame_portrait(s.character.object_type, s.character.skin,
                                          static_cast<std::uint32_t>(s.character.tex1),
                                          static_cast<std::uint32_t>(s.character.tex2), 200);
    } catch (const std::exception&) {
      portrait = {};
    }
    if (portrait.width > 0 && portrait.height > 0) {
      const double w = portrait.width / portrait.pixel_scale;
      const double h = portrait.height / portrait.pixel_scale;
      element->SetProperty("display", "block");
      element->SetProperty("width", dp(w));
      element->SetProperty("height", dp(h));
      element->SetProperty("left", dp(std::round((68 - w) / 2)));
      element->SetProperty("top", dp(std::round(5 + (80 - h) / 2)));
      ui::set_native_image(*element, portrait, ++portrait_revision);
      ui::set_native_source(*element, {0, 0, portrait.width, portrait.height});
    } else {
      element->SetProperty("display", "none");
    }
    rebuild();
  }

  // StatsLine: the title row, then label and value; odd rows get the cut-edge background.
  std::string line_rml(int index, std::string_view label, std::string_view value,
                       std::string_view value_class, int tip, bool lock = false) {
    std::string result = "<div class=\"line" + std::string(index % 2 ? " odd" : "") + "\"";
    if (tip >= 0) {
      result += " data-tip=\"" + std::to_string(tip) + "\"";
    }
    result += "><div class=\"row\"></div><div class=\"label t13b\">" + escaped(label) + "</div>";
    if (lock) {
      // StatsLine: the padlock at (lineWidth - 36 + 5, -8).
      const auto& image = art.image("famelock");
      result += "<img class=\"lock\" src=\"art:famelock\" style=\"width:" +
                dp(image.width / image.pixel_scale) + ";height:" +
                dp(image.height / image.pixel_scale) + "\"/>";
    } else {
      result += "<div class=\"value t13b " + std::string(value_class) + "\">" + escaped(value) +
                "</div>";
    }
    return result + "</div>";
  }

  int add_tip(std::string title, std::string body) {
    tips.push_back({std::move(title), std::move(body)});
    return static_cast<int>(tips.size()) - 1;
  }

  void rebuild() {
    tips.clear();
    hide_tip();
    sync_tabs();
    if (!stats) {
      lines->SetInnerRML("");
      return;
    }
    const auto& s = *stats;
    std::string html;
    if (tab == 0) {
      html += "<div class=\"line title\"><div class=\"label t15b\">Statistics</div></div>";
      int index = 1;
      for (const auto& line : stat_lines) {
        std::string value;
        int number = 0;
        if (std::string_view(line.label) == "Accuracy") {
          const double shots = s.stat("Shots");
          const double hits = s.stat("ShotsThatDamage");
          char text[32];
          std::snprintf(text, sizeof text, "%.2f%%", shots > 0 && hits > 0 ? hits / shots * 100 : 0.0);
          value = text;
          number = shots > 0 && hits > 0;
        } else {
          number = line.tag ? s.stat(line.tag) : s.dungeons_completed();
          value = std::to_string(number);
        }
        const int tip = line.tooltip ? add_tip({}, line.tooltip) : -1;
        html += line_rml(index++, line.label, value, number ? "" : "zero", tip);
      }
      html += "<div class=\"line title\"><div class=\"label t15b\">Bonuses</div></div>";
      ++index;
      // Unlocked bonuses by fame added, then locked ones by the level they need.
      std::vector<const FameBonusLine*> unlocked;
      std::vector<const FameBonusLine*> locked;
      for (const auto& bonus : s.bonuses) {
        (bonus.min_level <= s.character.level ? unlocked : locked).push_back(&bonus);
      }
      std::stable_sort(unlocked.begin(), unlocked.end(),
                       [](const auto* a, const auto* b) { return a->fame > b->fame; });
      std::stable_sort(locked.begin(), locked.end(),
                       [](const auto* a, const auto* b) { return a->min_level < b->min_level; });
      for (const auto* group : {&unlocked, &locked}) {
        for (const auto* bonus : *group) {
          const bool is_locked = group == &locked;
          const int tip =
              add_tip({}, bonus->description + "\nRequires level " +
                              std::to_string(bonus->min_level) + ".");
          html += line_rml(index++, bonus->name, "+" + std::to_string(bonus->fame),
                           bonus->fame ? "bonus" : "bonus zero", tip, is_locked);
        }
      }
    } else {
      html += "<div class=\"line title\"><div class=\"label t15b\">Dungeons</div></div>";
      auto dungeons = s.dungeons;
      std::sort(dungeons.begin(), dungeons.end());
      int index = 1;
      for (const auto& [label, count] : dungeons) {
        std::string icon;
        if (const auto* portal = catalog.object(portal_id(label))) {
          const std::string key = "fameportal" + std::to_string(portal->type);
          if (art.contains(key)) {
            const auto& image = art.image(key);
            const double w = image.width / image.pixel_scale;
            const double h = image.height / image.pixel_scale;
            // DungeonLine: x = 13 - round(w / 2), y = 11 - round(h / 2).
            icon = "<img class=\"portal\" src=\"art:" + key + "\" style=\"width:" + dp(w) +
                   ";height:" + dp(h) + ";left:" + dp(13 - std::round(w / 2)) +
                   ";top:" + dp(11 - std::round(h / 2)) + "\"/>";
          }
        }
        html += "<div class=\"line dungeon" + std::string(index % 2 ? " odd" : "") +
                "\"><div class=\"row\"></div>" + icon + "<div class=\"label t13b\">" +
                escaped(label) + "</div><div class=\"value t13b" + (count ? "" : " zero") + "\">" +
                std::to_string(count) + "</div></div>";
        ++index;
      }
    }
    lines->SetInnerRML(html);
    lines->SetScrollTop(0);
  }

  void sync_tabs() {
    auto* tabs = document->GetElementById("tabs");
    for (int i = 0; i < tabs->GetNumChildren(); ++i) {
      auto* child = tabs->GetChild(i);
      child->SetClass("open", child->GetAttribute<int>("data-index", -1) == tab);
    }
  }

  void on_click(Rml::Event& event) {
    auto* element = holder(event.GetTargetElement(), "data-action");
    if (!element) {
      return;
    }
    const auto action = element->GetAttribute<Rml::String>("data-action", "");
    if (action == "close") {
      visible_ = false;
    } else if (action == "tab") {
      const int index = element->GetAttribute<int>("data-index", 0);
      if (index != tab) {
        tab = index;
        rebuild();
      }
    }
  }

  // HoverTooltipDelegate: a TextToolTip beside the pointer over a line or the info button.
  void on_hover(Rml::Event& event) {
    auto* element = holder(event.GetTargetElement(), "data-tip");
    if (!element) {
      hide_tip();
      return;
    }
    const auto key = element->GetAttribute<Rml::String>("data-tip", "");
    const Tip* tip = nullptr;
    Tip info{"Fame calculation", "Refreshes each time the overview is opened."};
    if (key == "info") {
      tip = &info;
    } else {
      const int index = element->GetAttribute<int>("data-tip", -1);
      if (index >= 0 && index < static_cast<int>(tips.size())) {
        tip = &tips[static_cast<std::size_t>(index)];
      }
    }
    if (!tip) {
      hide_tip();
      return;
    }
    if (shown_tip != key) {
      shown_tip = key;
      auto* title = document->GetElementById("tip-title");
      title->SetInnerRML(escaped(tip->title));
      title->SetProperty("display", tip->title.empty() ? "none" : "block");
      std::string body = escaped(tip->body);
      for (std::size_t at = body.find('\n'); at != std::string::npos; at = body.find('\n', at)) {
        body.replace(at, 1, "<br/>");
      }
      document->GetElementById("tip-body")->SetInnerRML(body);
      tooltip->SetProperty("display", "block");
    }
    // The tooltip lives in #popup: place it in popup space, right of and below the pointer.
    auto* popup = document->GetElementById("popup");
    const double scale = host.scale();
    const double x = event.GetParameter<float>("mouse_x", 0) - popup->GetAbsoluteLeft();
    const double y = event.GetParameter<float>("mouse_y", 0) - popup->GetAbsoluteTop();
    tooltip->SetProperty("left", dp(std::min(x / scale + 12, 362.0 - 200)));
    tooltip->SetProperty("top", dp(y / scale + 16));
  }

  void hide_tip() {
    if (!shown_tip.empty()) {
      shown_tip.clear();
      tooltip->SetProperty("display", "none");
    }
  }

  void update(bool showable) {
    poll();
    if (!visible_ || !showable) {
      if (shown) {
        hide_tip();
        host.hide(*document);
        shown = false;
      }
      return;
    }
    if (!shown) {
      host.show(*document, true, false);
      shown = true;
    }
  }

  struct Pending {
    std::string address;
    std::uint16_t port{};
    net::FormFields fields;
    std::uint64_t generation{};
  };

  ui::RmlHost& host;
  const Catalog& catalog;
  const HudArt& art;
  Rml::ElementDocument* document{};
  Rml::Element* lines{};
  Rml::Element* tooltip{};
  std::future<std::string> request;
  std::optional<Pending> pending;
  std::uint64_t generation{};
  std::uint64_t request_generation{};
  std::optional<CharacterFameStats> stats;
  std::vector<Tip> tips;
  std::string shown_tip;
  Image portrait;
  std::uint64_t portrait_revision{};
  int tab{};
  bool visible_{};
  bool shown{};
};

FameContentPopup::FameContentPopup(ui::RmlHost& host, const Catalog& catalog, const HudArt& art)
    : impl_(std::make_unique<Impl>(host, catalog, art)) {}

FameContentPopup::~FameContentPopup() = default;

void FameContentPopup::open(std::string host, std::uint16_t port, net::FormFields credentials,
                            int character_id) {
  impl_->open(std::move(host), port, std::move(credentials), character_id);
}

void FameContentPopup::close() {
  impl_->visible_ = false;
}

bool FameContentPopup::visible() const {
  return impl_->visible_;
}

void FameContentPopup::update(bool showable) {
  impl_->update(showable);
}

Rml::ElementDocument* FameContentPopup::document() const {
  return impl_->document;
}
}
