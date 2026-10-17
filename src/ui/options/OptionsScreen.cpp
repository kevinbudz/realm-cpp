#include "realm/ui/options/OptionsScreen.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace realm {
namespace {
// The slider's drawn track and its thumb, in dp (options.rcss).
constexpr double slider_width = 170;
constexpr double thumb_width = 12;
// A dropdown's padding around its text (options.rcss: 16 dp before it, 34 dp after for the arrow).
constexpr double choice_padding = 16 + 34;
// The cursor sheet: a 4x4 grid of 32 px cursors (CursorSet).
constexpr int cursor_size = 32;
constexpr std::string_view default_hint = "Hover over a setting to see what it does.";

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
  return std::to_string(std::round(value * 100) / 100) + "dp";
}

// The element (or nearest ancestor) carrying an attribute: text and control internals can be
// the event target.
Rml::Element* holder(Rml::Element* element, const char* attribute) {
  while (element && !element->HasAttribute(attribute)) {
    element = element->GetParentNode();
  }
  return element;
}

Rml::ElementFormControl* form(Rml::Element* element) {
  return dynamic_cast<Rml::ElementFormControl*>(element);
}

int choice_index(const OptionDef& option, int value) {
  for (std::size_t i = 0; i < option.choices.size(); ++i) {
    if (option.choices[i].second == value) {
      return static_cast<int>(i);
    }
  }
  return 0;
}

// A gallery choice: one tile per value, the image set's picture or the choice's own label.
std::string gallery_rml(const OptionDef& option) {
  std::string result = "<div class=\"gallery\">";
  for (const auto& [label, choice] : option.choices) {
    result += "<div class=\"tile\" data-action=\"choice\" data-param=\"" + escaped(option.param) +
              "\" data-value=\"" + std::to_string(choice) + "\">" +
              (choice >= 0 ? "<native-image data-value=\"" + std::to_string(choice) + "\"/>"
                           : escaped(label == "System" ? std::string_view("OS") : label)) +
              "</div>";
  }
  return result + "</div>";
}

std::string control_rml(const OptionDef& option, int value) {
  const auto param = escaped(option.param);
  switch (option.kind) {
    case OptionDef::Kind::key:
      return "<div class=\"key\" data-action=\"key\" data-param=\"" + param + "\"></div>";
    case OptionDef::Kind::toggle:
      return "<div class=\"switch\" data-action=\"toggle\" data-param=\"" + param +
             "\"><div class=\"knob\"></div></div>";
    case OptionDef::Kind::choice: {
      std::string result = "<select class=\"choice\" data-param=\"" + param + "\">";
      for (const auto& [label, choice] : option.choices) {
        result += "<option value=\"" + std::to_string(choice) + "\"" +
                  (choice == value ? " selected" : "") + ">" + escaped(label) + "</option>";
      }
      return result + "</select>";
    }
    case OptionDef::Kind::slider:
      return "<div class=\"slider\"><div class=\"track\"></div><div class=\"fill\"></div>"
             "<input type=\"range\" data-param=\"" +
             param + "\" min=\"" + std::to_string(option.min) + "\" max=\"" +
             std::to_string(slider_end(option)) + "\" step=\"" + std::to_string(option.step) +
             "\" value=\"" + std::to_string(slider_position(option, value)) +
             "\"/></div><div class=\"readout\"></div>";
    case OptionDef::Kind::hidden_key:
    case OptionDef::Kind::hidden_value:
      return {};
  }
  return {};
}
}

struct OptionsScreen::Impl {
  struct Node {
    const OptionDef* def{};
    Rml::Element* row{};
    Rml::Element* control{};
    Rml::Element* fill{};
    Rml::Element* readout{};
    Rml::ElementList tiles;
    std::string text;
  };

  Impl(OptionsMenu& options, ui::RmlHost& ui_host) : menu(options), host(ui_host) {
    document = host.load("options");
    if (!document) {
      throw std::runtime_error("The RmlUi options document did not load");
    }
    search = document->GetElementById("search");
    search_clear = document->GetElementById("search-clear");
    nav = document->GetElementById("nav");
    rows = document->GetElementById("rows");
    hint = document->GetElementById("hint");
    reset = document->GetElementById("reset");
    reset_label = document->GetElementById("reset-label");
    if (!search || !search_clear || !nav || !rows || !hint || !reset || !reset_label) {
      throw std::runtime_error("The RmlUi options document is missing its parts");
    }
    // The key box listens for any stage MOUSE_DOWN (capture phase) to leave input mode.
    host.listen(*document, "mousedown", [this](Rml::Event&) { menu.cancel_capture(); }, true);
    host.listen(*document, "click", [this](Rml::Event& event) { on_click(event); }, true);
    host.listen(*document, "change", [this](Rml::Event& event) { on_change(event); }, true);
    host.listen(
        *document, "mouseover",
        [this](Rml::Event& event) { set_hint(holder(event.GetTargetElement(), "data-row")); },
        true);
    host.listen(*reset, "mouseout", [this](Rml::Event& event) {
      if (!reset->IsPointWithinElement(event.GetUnprojectedMouseScreenPos())) {
        arm_reset(false);
      }
    });
  }

  ~Impl() { host.close(document); }

  void on_click(Rml::Event& event) {
    auto* element = holder(event.GetTargetElement(), "data-action");
    if (!element) {
      return;
    }
    const auto action = element->GetAttribute<Rml::String>("data-action", "");
    const auto param = element->GetAttribute<Rml::String>("data-param", "");
    const auto index = element->GetAttribute<int>("data-index", 0);
    if (action != "reset") {
      arm_reset(false);
    }
    if (action == "category" && index >= 0 && index < static_cast<int>(options_category_count)) {
      menu.set_category(static_cast<OptionsCategory>(index));
      search->Blur();
    } else if (action == "section" && index >= 0 && index < static_cast<int>(sections.size())) {
      rows->SetScrollTop(section_top(static_cast<std::size_t>(index)));
    } else if (action == "clear-search") {
      menu.set_query({});
      search->Blur();
    } else if (action == "close") {
      menu.request_close();
    } else if (action == "home") {
      menu.request_home();
    } else if (action == "reset") {
      if (reset_armed) {
        menu.reset();
      }
      arm_reset(!reset_armed);
    } else if (action == "key") {
      menu.begin_capture(param);
    } else if (action == "choice") {
      menu.set_value(param, element->GetAttribute<int>("data-value", 0));
    } else if (action == "toggle") {
      menu.toggle(param);
    }
  }

  void on_change(Rml::Event& event) {
    if (syncing) {
      return;
    }
    auto* target = event.GetTargetElement();
    if (target == search) {
      menu.set_query(form(search)->GetValue());
      return;
    }
    const auto param = target->GetAttribute<Rml::String>("data-param", "");
    const auto* option = find_option(param);
    if (!option) {
      return;
    }
    if (option->kind == OptionDef::Kind::slider) {
      const auto position = static_cast<int>(std::lround(event.GetParameter<float>("value", 0)));
      menu.set_value(param, slider_value(*option, position));
    } else if (option->kind == OptionDef::Kind::choice) {
      const auto value = form(target)->GetValue();
      try {
        menu.set_value(param, std::stoi(value));
      } catch (const std::exception&) {
      }
    }
  }

  // Reset to Defaults resets every setting, so its first click only arms it.
  void arm_reset(bool armed) {
    if (armed == reset_armed) {
      return;
    }
    reset_armed = armed;
    reset->SetClass("armed", armed);
    reset_label->SetInnerRML(armed ? "Click again to reset" : "Reset to Defaults");
  }

  void set_hint(Rml::Element* row) {
    const OptionDef* option =
        row ? find_option(row->GetAttribute<Rml::String>("data-row", "")) : nullptr;
    if (option == hovered) {
      return;
    }
    hovered = option;
    hint->SetInnerRML(escaped(option ? option->tooltip : default_hint));
  }

  void update(bool showable) {
    if (!menu.visible() || !showable) {
      if (shown) {
        arm_reset(false);
        search->Blur();
        host.hide(*document);
        shown = false;
      }
      return;
    }
    if (!shown) {
      host.show(*document, true, false);
      shown = true;
    }
    if (menu.layout_revision() != layout) {
      rebuild();
      fit_choices();
      layout = menu.layout_revision();
      values = ~std::uint64_t{};
    }
    if (menu.value_revision() != values) {
      sync();
      values = menu.value_revision();
    }
    track_section();
  }

  void rebuild() {
    syncing = true;
    if (auto* field = form(search); field->GetValue() != menu.query()) {
      field->SetValue(menu.query());
    }
    search_clear->SetClass("shown", menu.searching());

    std::string html;
    for (std::size_t i = 0; i < options_category_count; ++i) {
      const auto category = static_cast<OptionsCategory>(i);
      const bool active = !menu.searching() && category == menu.category();
      html += "<div class=\"category" + std::string(active ? " active" : "") +
              "\" data-action=\"category\" data-index=\"" + std::to_string(i) + "\">" +
              escaped(options_category_name(category)) + "</div>";
      if (active) {
        html += "<div class=\"section-links\">";
        int index = 0;
        for (const auto section : category_sections(category)) {
          html += "<div class=\"section-link\" data-action=\"section\" data-index=\"" +
                  std::to_string(index++) + "\">" + escaped(section) + "</div>";
        }
        html += "</div>";
      }
    }
    nav->SetInnerRML(html);

    html.clear();
    const OptionDef* previous = nullptr;
    for (const auto* option : menu.rows()) {
      if (!previous || previous->section != option->section ||
          previous->category != option->category) {
        if (previous) {
          html += "</div>";
        }
        std::string heading(option->section);
        if (menu.searching()) {
          heading = std::string(options_category_name(option->category)) + " \xC2\xB7 " + heading;
        }
        html +=
            "<div class=\"section\"><div class=\"section-title\">" + escaped(heading) +
            "</div><div class=\"deca-divider\"></div>";
      }
      previous = option;
      if (gallery(*option)) {
        html += "<div class=\"row gallery-row\" data-row=\"" + escaped(option->param) +
                "\"><div class=\"label\">" + escaped(option->label) + "</div>" +
                gallery_rml(*option) + "</div>";
      } else {
        html += "<div class=\"row\" data-row=\"" + escaped(option->param) +
                "\"><div class=\"label\">" + escaped(option->label) +
                "</div><div class=\"control\">" + control_rml(*option, menu.value(*option)) +
                "</div></div>";
      }
    }
    if (previous) {
      html += "</div>";
    } else {
      html = "<div class=\"empty\">No settings match \"" + escaped(menu.query()) + "\".</div>";
    }
    rows->SetInnerRML(html);
    rows->SetScrollTop(0);

    nodes.clear();
    Rml::ElementList found;
    rows->GetElementsByClassName(found, "row");
    for (auto* row : found) {
      Node node;
      node.def = find_option(row->GetAttribute<Rml::String>("data-row", ""));
      node.row = row;
      node.control = row->QuerySelector(".key, .switch, select, input, .gallery");
      node.fill = row->QuerySelector(".fill");
      node.readout = row->QuerySelector(".readout");
      row->GetElementsByClassName(node.tiles, "tile");
      Rml::ElementList images;
      row->GetElementsByTagName(images, "native-image");
      for (auto* image : images) {
        const int index = image->GetAttribute<int>("data-value", 0);
        ui::set_native_image(*image, cursors, 1);
        ui::set_native_source(
            *image, {index % 4 * cursor_size, index / 4 * cursor_size, cursor_size, cursor_size});
      }
      if (node.def && node.control) {
        nodes.push_back(node);
      }
    }
    sections.clear();
    rows->GetElementsByClassName(sections, "section");
    links.clear();
    nav->GetElementsByClassName(links, "section-link");
    current = -1;
    hovered = nullptr;
    hint->SetInnerRML(escaped(menu.searching() ? std::string_view{} : default_hint));
    syncing = false;
  }

  // Each dropdown is as wide as its longest choice, in the select's own (bold) font, so the
  // list beneath it is too.
  void fit_choices() {
    document->UpdateDocument();
    const auto* context = document->GetContext();
    const double ratio = context ? context->GetDensityIndependentPixelRatio() : 1;
    for (const auto& node : nodes) {
      if (node.def->kind != OptionDef::Kind::choice || !node.control->IsClassSet("choice")) {
        continue;
      }
      int widest = 0;
      for (const auto& choice : node.def->choices) {
        widest = std::max(widest, Rml::ElementUtilities::GetStringWidth(
                                      node.control, Rml::String(choice.first)));
      }
      node.control->SetProperty("width", dp(std::ceil(widest / ratio) + choice_padding));
    }
  }

  void sync() {
    syncing = true;
    for (auto& node : nodes) {
      const auto& option = *node.def;
      const int value = menu.value(option);
      const bool enabled = menu.enabled(option);
      node.row->SetClass("disabled", !enabled);
      switch (option.kind) {
        case OptionDef::Kind::key: {
          const bool capturing = menu.capture() == option.param;
          const auto text = capturing ? std::string("Press a key") : value_text(option, value);
          if (text != node.text) {
            node.control->SetInnerRML(escaped(text));
            node.text = text;
          }
          node.control->SetClass("capturing", capturing);
          node.control->SetClass("unset", !capturing && value == key_unset);
          break;
        }
        case OptionDef::Kind::toggle:
          node.control->SetClass("on", value != 0);
          break;
        case OptionDef::Kind::choice:
          for (auto* tile : node.tiles) {
            tile->SetClass("selected", tile->GetAttribute<int>("data-value", 0) == value);
          }
          if (auto* select = dynamic_cast<Rml::ElementFormControlSelect*>(node.control);
              select && select->GetSelection() != choice_index(option, value)) {
            select->SetSelection(choice_index(option, value));
          }
          break;
        case OptionDef::Kind::slider: {
          const int position = slider_position(option, value);
          if (auto* input = form(node.control);
              input && input->GetValue() != std::to_string(position)) {
            input->SetValue(std::to_string(position));
          }
          const int span = std::max(1, slider_end(option) - option.min);
          const double fraction = static_cast<double>(position - option.min) / span;
          if (node.fill) {
            node.fill->SetProperty("width",
                                   dp(thumb_width / 2 + fraction * (slider_width - thumb_width)));
          }
          if (node.readout) {
            // The top_label stop (the infinity sign) is a symbol, drawn larger than the numbers.
            node.readout->SetClass("symbol", !option.top_label.empty() && value == 0);
          }
          if (const auto text = value_text(option, value); node.readout && text != node.text) {
            node.readout->SetInnerRML(escaped(text));
            node.text = text;
          }
          break;
        }
        case OptionDef::Kind::hidden_key:
        case OptionDef::Kind::hidden_value:
          break;
      }
    }
    syncing = false;
  }

  // A section's scroll position: its offset below the first section, so the rows' top padding (the gap above the
  // first title, which scrolls away) is not counted.
  float section_top(std::size_t index) const {
    return sections[index]->GetOffsetTop() - sections.front()->GetOffsetTop();
  }

  // The sidebar marks the section at the top of the scrolled rows.
  void track_section() {
    if (menu.searching() || sections.empty() || links.size() != sections.size()) {
      return;
    }
    const float top = rows->GetScrollTop();
    int next = 0;
    for (std::size_t i = 0; i < sections.size(); ++i) {
      if (section_top(i) <= top + 12) {
        next = static_cast<int>(i);
      }
    }
    if (top > 0 && top + rows->GetClientHeight() >= rows->GetScrollHeight() - 1) {
      next = static_cast<int>(sections.size()) - 1;
    }
    if (next != current) {
      for (std::size_t i = 0; i < links.size(); ++i) {
        links[i]->SetClass("current", static_cast<int>(i) == next);
      }
      current = next;
    }
  }

  // A gallery needs its image set; without the cursor sheet the cursor stays a dropdown.
  [[nodiscard]] bool gallery(const OptionDef& option) const {
    return option.kind == OptionDef::Kind::choice && option.preview == "cursors" &&
           cursors.width >= 4 * cursor_size && cursors.height >= 4 * cursor_size;
  }

  [[nodiscard]] bool wants_text() const {
    return shown && document->GetContext() && document->GetContext()->GetFocusElement() == search;
  }

  void leave_search() {
    if (menu.searching()) {
      menu.set_query({});
    } else {
      search->Blur();
    }
  }

  OptionsMenu& menu;
  ui::RmlHost& host;
  Rml::ElementDocument* document{};
  Rml::Element* search{};
  Rml::Element* search_clear{};
  Rml::Element* nav{};
  Rml::Element* rows{};
  Rml::Element* hint{};
  Rml::Element* reset{};
  Rml::Element* reset_label{};
  Image cursors;
  std::vector<Node> nodes;
  Rml::ElementList sections;
  Rml::ElementList links;
  const OptionDef* hovered{};
  std::uint64_t layout{~std::uint64_t{}};
  std::uint64_t values{~std::uint64_t{}};
  int current{-1};
  bool shown{};
  bool syncing{};
  bool reset_armed{};
};

OptionsScreen::OptionsScreen(OptionsMenu& menu, ui::RmlHost& host)
    : impl_(std::make_unique<Impl>(menu, host)) {}

OptionsScreen::~OptionsScreen() = default;

void OptionsScreen::set_cursors(Image sheet) {
  impl_->cursors = std::move(sheet);
  impl_->layout = ~std::uint64_t{};
}

void OptionsScreen::update(bool showable) {
  impl_->update(showable);
}

bool OptionsScreen::wants_text() const {
  return impl_->wants_text();
}

void OptionsScreen::leave_search() {
  impl_->leave_search();
}

Rml::ElementDocument* OptionsScreen::document() const {
  return impl_->document;
}
}
