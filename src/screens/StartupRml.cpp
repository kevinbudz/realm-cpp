#include "realm/screens/StartupRml.hpp"
#include "realm/mapeditor/EditorScreen.hpp"
#include "realm/util/Utf8.hpp"
#include "realm/util/TextFormat.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <functional>

namespace realm {
namespace {
using ActionKind = StartupAction::Kind;

std::string css_color(const std::array<float, 4>& color) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result = "#00000000";
  for (std::size_t i = 0; i < 4; ++i) {
    const auto value = static_cast<unsigned>(std::lround(std::clamp(color[i], 0.F, 1.F) * 255));
    result[1 + i * 2] = digits[(value >> 4U) & 15U];
    result[2 + i * 2] = digits[value & 15U];
  }
  return result;
}
std::string escaped(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const char c : value) {
    if (c == '&')
      result += "&amp;";
    else if (c == '<')
      result += "&lt;";
    else if (c == '>')
      result += "&gt;";
    else if (c == '\"')
      result += "&quot;";
    else if (c == '\'')
      result += "&#39;";
    else
      result += c;
  }
  return result;
}
std::string action_name(ActionKind kind) {
  switch (kind) {
    case ActionKind::play:
      return "play";
    case ActionKind::account:
      return "account";
    case ActionKind::account_logout:
      return "account-logout";
    case ActionKind::legends:
      return "legends";
    case ActionKind::legend_tab:
      return "legend-tab";
    case ActionKind::legend_row:
      return "legend-row";
    case ActionKind::editor:
      return "editor";
    case ActionKind::login:
      return "login";
    case ActionKind::register_account:
      return "register";
    case ActionKind::back:
      return "back";
    case ActionKind::classes:
      return "classes";
    case ActionKind::new_character:
      return "new-character";
    case ActionKind::buy_slot:
      return "buy-slot";
    case ActionKind::character:
      return "character";
    case ActionKind::class_type:
      return "class";
    case ActionKind::cancel:
      return "cancel";
    case ActionKind::submit:
      return "submit";
    case ActionKind::link:
      return "link";
    case ActionKind::field:
      return "field";
    case ActionKind::delete_character:
      return "delete-character";
    case ActionKind::news:
      return "news";
    case ActionKind::sound:
      return "sound";
    case ActionKind::none:
      return {};
  }
  return {};
}
std::optional<StartupAction> event_action(const Rml::Element* element) {
  // Text and input internals can be the event target. Actions belong to the containing control.
  while (element && !element->HasAttribute("data-action")) element = element->GetParentNode();
  if (!element) return std::nullopt;
  const auto name = element->GetAttribute<Rml::String>("data-action", "");
  const auto index = static_cast<std::size_t>(std::max(0, element->GetAttribute<int>("data-index", 0)));
  if (name == "play") return StartupAction{ActionKind::play};
  if (name == "account") return StartupAction{ActionKind::account};
  if (name == "account-logout") return StartupAction{ActionKind::account_logout};
  if (name == "legends") return StartupAction{ActionKind::legends};
  if (name == "legend-tab") return StartupAction{ActionKind::legend_tab, index};
  if (name == "legend-row") return StartupAction{ActionKind::legend_row, index};
  if (name == "editor") return StartupAction{ActionKind::editor};
  if (name == "login") return StartupAction{ActionKind::login};
  if (name == "register") return StartupAction{ActionKind::register_account};
  if (name == "back") return StartupAction{ActionKind::back};
  if (name == "classes") return StartupAction{ActionKind::classes};
  if (name == "new-character") return StartupAction{ActionKind::new_character};
  if (name == "buy-slot") return StartupAction{ActionKind::buy_slot};
  if (name == "character") return StartupAction{ActionKind::character, index};
  if (name == "class") return StartupAction{ActionKind::class_type, index};
  if (name == "cancel") return StartupAction{ActionKind::cancel};
  if (name == "submit") return StartupAction{ActionKind::submit};
  if (name == "link") return StartupAction{ActionKind::link};
  if (name == "field") return StartupAction{ActionKind::field, index};
  if (name == "delete-character") return StartupAction{ActionKind::delete_character, index};
  if (name == "news") return StartupAction{ActionKind::news, index};
  if (name == "sound") return StartupAction{ActionKind::sound};
  return std::nullopt;
}
bool acts_on_down(ActionKind kind) { return kind == ActionKind::new_character || kind == ActionKind::field; }
bool same_action(const std::optional<StartupAction>& a, const std::optional<StartupAction>& b) {
  return a && b && *a == *b;
}
EditorInputKey editor_key(int key, std::uint16_t modifiers) {
  if (key == SDLK_LSHIFT || key == SDLK_RSHIFT) return EditorInputKey::shift;
  if (key == SDLK_LCTRL || key == SDLK_RCTRL) return EditorInputKey::control;
  const bool control = (modifiers & KMOD_CTRL) != 0;
  if (control && key == SDLK_z) return EditorInputKey::undo;
  if (control && key == SDLK_y) return EditorInputKey::redo;
  switch (key) {
    case SDLK_ESCAPE:
      return EditorInputKey::escape;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      return EditorInputKey::enter;
    case SDLK_BACKSPACE:
      return EditorInputKey::backspace;
    case SDLK_TAB:
      return EditorInputKey::tab;
    case SDLK_LEFT:
      return EditorInputKey::left;
    case SDLK_RIGHT:
      return EditorInputKey::right;
    case SDLK_UP:
      return EditorInputKey::up;
    case SDLK_DOWN:
      return EditorInputKey::down;
    case SDLK_d:
      return EditorInputKey::draw;
    case SDLK_e:
      return EditorInputKey::erase;
    case SDLK_a:
      return EditorInputKey::sample;
    case SDLK_i:
      return EditorInputKey::name;
    case SDLK_c:
      return EditorInputKey::clear;
    case SDLK_l:
      return EditorInputKey::load;
    case SDLK_s:
      return EditorInputKey::save;
    case SDLK_t:
      return EditorInputKey::test;
    case SDLK_b:
      return EditorInputKey::back;
    case SDLK_EQUALS:
    case SDLK_PLUS:
      return EditorInputKey::zoom_in;
    case SDLK_MINUS:
      return EditorInputKey::zoom_out;
    case SDLK_u:
      return EditorInputKey::undo;
    case SDLK_r:
      return EditorInputKey::redo;
    default:
      return EditorInputKey::none;
  }
}

struct Node {
  Rml::Element* element{};
  std::size_t snapshot_index{};
  std::optional<StartupAction> action;
  Image brightened;
  HudRegion brightened_source{};
  std::uint64_t brightened_revision{};
  bool image{}, pulse{}, hovered{};
};
struct FieldNode {
  Rml::ElementFormControlInput* element{};
  std::size_t index{};
  bool password{};
};
struct NodeSignature {
  int parent{};  // screen, dialog, or input
  int type{};
  std::optional<StartupAction> action;
  friend bool operator==(const NodeSignature&, const NodeSignature&) = default;
};
struct SavedField {
  std::size_t index{};
  int selection_start{}, selection_end{};
  bool focused{};
};
}

struct StartupRml::Impl {
  Impl(Startup& model, ui::RmlHost& ui_host) : startup(model), host(ui_host) {
    document = host.load("startup");
    if (!document) throw std::runtime_error("The RmlUi startup document did not load");
    screen = document->GetElementById("screen");
    skin = document->GetElementById("skin");
    dialog = document->GetElementById("dialog");
    if (!screen || !skin || !dialog) throw std::runtime_error("The RmlUi startup document is missing its layers");
    listen(*document, "mousedown", [this](Rml::Event& event) { on_down(event); }, true);
    listen(*document, "click", [this](Rml::Event& event) { on_click(event); }, true);
    listen(*document, "change", [this](Rml::Event& event) { sync_field(event.GetTargetElement()); }, true);
    listen(
        *document, "focus",
        [this](Rml::Event& event) {
          if (!suppress_field_focus)
            if (auto action = event_action(event.GetTargetElement()); action && action->kind == ActionKind::field)
              this->startup.perform(*action);
        },
        true);
    listen(
        *document, "mouseover", [this](Rml::Event& event) { set_hover(event_action(event.GetTargetElement())); }, true);
    listen(*document, "mouseout", [this](Rml::Event&) { set_hover(std::nullopt); }, true);
  }
  ~Impl() { host.close(document); }

  void listen(Rml::Element& element, const char* name, std::function<void(Rml::Event&)> callback,
              bool capture = false) {
    host.listen(element, name, std::move(callback), capture);
  }
  void update() {
    if (auto url = startup.take_open_url()) SDL_OpenURL(url->c_str());
    const auto& view = startup.view();
    if (!view.menus) {
      if (document->IsVisible()) host.hide(*document);
      shown = false;
      modal = false;
      return;
    }
    const bool next_modal = view.dialog || view.skin_modal_input;
    if (!shown || modal != next_modal) {
      host.show(*document, next_modal, false);
      shown = true;
      modal = next_modal;
    }
    const auto next_signature = signature(view);
    if (next_signature != signature_) {
      rebuild(view, next_signature);
      signature_ = next_signature;
    }
    if (view.revision != revision_ || view.sheet_revision != sheet_revision_ || view.skin_revision != skin_revision_)
      sync(view);
  }
  [[nodiscard]] Rml::ElementDocument* get_document() const { return document; }
  [[nodiscard]] bool wants_text() const {
    return startup.editor_wants_text() || startup.wants_text() || host.layer().wants_text();
  }

  [[nodiscard]] std::vector<NodeSignature> signature(const StartupViewSnapshot& view) const {
    std::vector<NodeSignature> result;
    result.reserve(view.elements.size() + view.fields.size());
    for (std::size_t i = 0; i <= view.elements.size(); ++i) {
      for (const auto& field : view.fields)
        if (field.insert_before == i) result.push_back({2, 5, StartupAction{ActionKind::field, field.index}});
      if (i == view.elements.size()) break;
      const auto& item = view.elements[i];
      if (item.field_value) continue;
      // The editable field itself replaces the native transparent hit rectangle.
      if (item.hit && item.action && item.action->kind == ActionKind::field) continue;
      const int type = item.bitmap                        ? 2
                       : item.hit                         ? 4
                       : item.pulse && !item.text.empty() ? 3
                       : item.solid                       ? 0
                       : !item.text.empty()               ? 1
                                                          : 2;
      const int parent = i < view.dialog_insertion ? 0 : 1;
      result.push_back({parent, type, item.action});
    }
    return result;
  }
  [[nodiscard]] Rml::ElementPtr create_element(const NodeSignature& value) {
    const char* tag = value.type == 5   ? "input"
                      : value.type == 3 ? "pulse-text"
                      : value.type == 2 ? "native-image"
                                        : "div";
    return document->CreateElement(tag);
  }
  void set_position(Rml::Element& element, Vec2 position, Vec2 size, double scale) {
    element.SetProperty("position", "absolute");
    element.SetProperty("left", trimmed_number(position.x / scale) + "dp");
    element.SetProperty("top", trimmed_number(position.y / scale) + "dp");
    element.SetProperty("width", trimmed_number(size.x / scale) + "dp");
    element.SetProperty("height", trimmed_number(size.y / scale) + "dp");
    element.SetProperty("box-sizing", "border-box");
    element.SetProperty("pointer-events", "auto");
  }
  [[nodiscard]] static bool needs_bright_image(const StartupElementSnapshot& item) {
    return std::any_of(item.hover_color.begin(), item.hover_color.begin() + 3, [](float value) { return value > 1.F; });
  }
  void prepare_bright_image(Node& node, const StartupElementSnapshot& item, const StartupViewSnapshot& view) {
    const auto source = item.source;
    if (!needs_bright_image(item) || source.width <= 0 || source.height <= 0) return;
    if (node.brightened_revision == view.sheet_revision && node.brightened_source.x == source.x &&
        node.brightened_source.y == source.y && node.brightened_source.width == source.width &&
        node.brightened_source.height == source.height)
      return;
    const auto& sheet = startup.sheet();
    node.brightened = {source.width, source.height,
                       std::vector<unsigned char>(static_cast<std::size_t>(source.width) *
                                                  static_cast<std::size_t>(source.height) * 4),
                       sheet.pixel_scale};
    for (int y = 0; y < source.height; ++y)
      for (int x = 0; x < source.width; ++x) {
        const auto from = (static_cast<std::size_t>(source.y + y) * static_cast<std::size_t>(sheet.width) +
                           static_cast<std::size_t>(source.x + x)) *
                          4;
        const auto to =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(source.width) + static_cast<std::size_t>(x)) * 4;
        for (std::size_t channel = 0; channel < 3; ++channel)
          node.brightened.pixels[to + channel] = static_cast<unsigned char>(
              std::clamp(std::lround(sheet.pixels[from + channel] * item.hover_color[channel]), 0L, 255L));
        node.brightened.pixels[to + 3] =
            static_cast<unsigned char>(std::clamp(std::lround(sheet.pixels[from + 3] * item.hover_color[3]), 0L, 255L));
      }
    node.brightened_source = source;
    node.brightened_revision = view.sheet_revision;
  }
  void set_node_color(Node& node, const StartupElementSnapshot& item, const StartupViewSnapshot& view) {
    const bool over = node.hovered && item.hover_color[3] > 0;
    if (node.image) {
      if (item.bitmap) {
        ui::set_native_image(*node.element, *item.bitmap, item.bitmap_revision);
        ui::set_native_source(*node.element, item.source);
        ui::set_native_tint(*node.element, over ? item.hover_color : item.color);
        return;
      }
      if (over && needs_bright_image(item)) {
        prepare_bright_image(node, item, view);
        ui::set_native_image(*node.element, node.brightened, view.sheet_revision);
        ui::set_native_source(*node.element, {0, 0, node.brightened.width, node.brightened.height});
        ui::set_native_tint(*node.element, {1, 1, 1, 1});
      } else {
        ui::set_native_image(*node.element, startup.sheet(), view.sheet_revision, view.sheet_dirty);
        ui::set_native_source(*node.element, item.source);
        ui::set_native_tint(*node.element, over ? item.hover_color : item.color);
      }
    } else if (node.pulse || !item.text.empty())
      node.element->SetProperty("color", css_color(over ? item.hover_color : item.font_color));
    else
      node.element->SetProperty("background-color", css_color(over ? item.hover_color : item.color));
  }
  void set_action(Rml::Element& element, const std::optional<StartupAction>& action) {
    if (!action) {
      element.RemoveAttribute("data-action");
      element.RemoveAttribute("data-index");
      return;
    }
    element.SetAttribute("data-action", action_name(action->kind));
    element.SetAttribute("data-index", static_cast<int>(action->index));
  }
  void rebuild(const StartupViewSnapshot& view, const std::vector<NodeSignature>& next_signature) {
    bool same_fields = fields.size() == view.fields.size();
    for (std::size_t i = 0; same_fields && i < fields.size(); ++i)
      same_fields = fields[i].index == view.fields[i].index && fields[i].password == view.fields[i].password;
    saved_fields.clear();
    if (same_fields) {
      auto* focused = host.context().GetFocusElement();
      for (const auto& field : fields) {
        int start{}, end{};
        field.element->GetSelection(&start, &end, nullptr);
        saved_fields.push_back({field.index, start, end, field.element == focused});
      }
    }
    restore_fields_pending = true;
    suppress_field_focus = true;
    screen->SetInnerRML("");
    dialog->SetInnerRML("");
    nodes.clear();
    fields.clear();
    nodes.reserve(next_signature.size());
    fields.reserve(view.fields.size());
    std::size_t signature_index{};
    for (std::size_t i = 0; i <= view.elements.size(); ++i) {
      for (const auto& field : view.fields)
        if (field.insert_before == i) {
          auto element = create_element(next_signature[signature_index++]);
          if (!element) throw std::runtime_error("Could not create a RmlUi startup field");
          auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element.get());
          if (!input) throw std::runtime_error("RmlUi startup field is not an input control");
          element->SetAttribute("type", field.password ? "password" : "text");
          element->SetAttribute("maxlength", 64);
          set_action(*element, StartupAction{ActionKind::field, field.index});
          auto* parent = i < view.dialog_insertion ? screen : dialog;
          parent->AppendChild(std::move(element));
          fields.push_back({input, field.index, field.password});
        }
      if (i == view.elements.size()) break;
      const auto& item = view.elements[i];
      if (item.field_value || (item.hit && item.action && item.action->kind == ActionKind::field)) continue;
      const int type = item.bitmap                        ? 2
                       : item.hit                         ? 4
                       : item.pulse && !item.text.empty() ? 3
                       : item.solid                       ? 0
                       : !item.text.empty()               ? 1
                                                          : 2;
      auto element = create_element(next_signature[signature_index++]);
      if (!element) throw std::runtime_error("Could not create a RmlUi startup element");
      element->SetAttribute("id", "startup-" + std::to_string(nodes.size()));
      set_action(*element, item.action);
      auto* parent = i < view.dialog_insertion ? screen : dialog;
      auto* raw = parent->AppendChild(std::move(element));
      nodes.push_back({raw, i, item.action, {}, {}, 0, type == 2, type == 3, false});
    }
    suppress_field_focus = false;
  }
  void sync(const StartupViewSnapshot& view) {
    const double scale = view.scale > 0 ? view.scale : 1;
    dialog->SetProperty("display", view.dialog ? "block" : "none");
    std::size_t node_index{};
    for (std::size_t i = 0; i < view.elements.size(); ++i) {
      const auto& item = view.elements[i];
      if (item.field_value || (item.hit && item.action && item.action->kind == ActionKind::field)) continue;
      if (node_index >= nodes.size()) break;
      auto& node = nodes[node_index++];
      node.snapshot_index = i;
      node.action = item.action;
      auto& element = *node.element;
      const Vec2 position = (item.text.empty() || item.solid || item.hit) ? item.position : item.text_position;
      Vec2 size = item.size;
      Vec2 placed = position;
      if (view.deletion && node.image && i >= view.dialog_insertion && std::abs(size.x - item.source.width) < 1 &&
          std::abs(size.y - item.source.height) < 1) {
        // The retained dialog bitmap is rasterized at drawable density: keep it on whole pixels, as the native build()
        // does.
        placed = {std::round(position.x), std::round(position.y)};
        size = {static_cast<double>(item.source.width), static_cast<double>(item.source.height)};
      }
      if (!item.text.empty() && !item.solid && !item.hit) {
        const auto field = host.fonts().layout(item.text, item.font_size, item.font_bold);
        size = {field.width * scale, field.height * scale};
      }
      if (node.pulse) {
        element.SetAttribute("text", item.text);
        element.SetAttribute("size", item.font_size);
        element.SetAttribute("bold", item.font_bold ? "true" : "false");
        element.SetAttribute("pulse", item.pulse ? "1" : "0");
        element.SetProperty("white-space", "nowrap");
        element.SetProperty("font-effect", item.font_shadow ? "shadow(0px 0px #000)" : "none");
      } else if (!item.text.empty() && !item.solid && !item.hit) {
        element.SetClass("t" + std::to_string(item.font_size) + (item.font_bold ? "b" : ""), true);
        element.SetProperty("white-space", "nowrap");
        element.SetProperty("font-effect", item.font_shadow ? "shadow(0px 0px #000)" : "none");
        const auto markup = escaped(item.text);
        if (element.GetInnerRML() != markup) element.SetInnerRML(markup);
      }
      set_position(element, placed, size, scale);
      // Decorative labels sit above their transparent control hit boxes. They must not steal the press.
      element.SetProperty("pointer-events",
                          !item.action || (view.dialog && i < view.dialog_insertion) ? "none" : "auto");
      if (item.solid || item.hit) element.SetProperty("background-color", css_color(item.color));
      node.hovered = tracks_hover(view) ? item.hovered : same_action(hovered, item.action);
      if (node.image || !item.text.empty() || (item.solid && !item.hit)) set_node_color(node, item, view);
      set_action(element, item.action);
    }
    for (auto& field : fields) {
      const auto found = std::find_if(view.fields.begin(), view.fields.end(),
                                      [&](const auto& item) { return item.index == field.index; });
      if (found == view.fields.end()) continue;
      const Vec2 size = found->size;
      set_position(*field.element, found->position, size, scale);
      field.element->SetDisabled(view.busy);
      field.element->SetProperty("border", "1dp #454545");
      field.element->SetProperty("background-color", "#333333");
      field.element->SetProperty("color", "#b3b3b3");
      field.element->SetProperty("font-size", "20dp");
      field.element->SetProperty("line-height", trimmed_number(host.fonts().layout("x", 20, false).height) + "dp");
      field.element->SetClass("t20", true);
      field.element->SetProperty("padding", "4dp 0 0 7dp");
      field.element->SetProperty("caret-color", "#b3b3b3");
      field.element->SetProperty("overflow", "hidden");
      const auto accepted = startup.field_value(field.index);
      if (field.element->GetValue() != accepted) field.element->SetValue(accepted);
      set_action(*field.element, StartupAction{ActionKind::field, field.index});
    }
    if (restore_fields_pending) {
      const SavedField* focused_saved = nullptr;
      if (saved_fields.size() == fields.size()) {
        for (const auto& saved : saved_fields) {
          const auto found =
              std::find_if(fields.begin(), fields.end(), [&](const auto& field) { return field.index == saved.index; });
          if (found == fields.end()) continue;
          const auto accepted = startup.field_value(saved.index);
          const int end = static_cast<int>(code_points(accepted));
          const int start = std::clamp(saved.selection_start, 0, end);
          const int finish = std::clamp(saved.selection_end, start, end);
          found->element->SetSelectionRange(start, finish);
          if (saved.focused) focused_saved = &saved;
        }
      }
      const FieldNode* focused_field = nullptr;
      if (focused_saved) {
        const auto found = std::find_if(fields.begin(), fields.end(),
                                        [&](const auto& field) { return field.index == focused_saved->index; });
        if (found != fields.end()) focused_field = &*found;
      }
      if (!focused_field)
        for (const auto& field : fields) {
          const auto found = std::find_if(view.fields.begin(), view.fields.end(),
                                          [&](const auto& item) { return item.index == field.index; });
          if (found != view.fields.end() && found->focused) {
            focused_field = &field;
            break;
          }
        }
      if (focused_field) {
        suppress_field_focus = true;
        focused_field->element->Focus();
        const auto saved = std::find_if(saved_fields.begin(), saved_fields.end(),
                                        [&](const auto& item) { return item.index == focused_field->index; });
        const int end = static_cast<int>(code_points(startup.field_value(focused_field->index)));
        const int start = saved == saved_fields.end() ? end : std::clamp(saved->selection_start, 0, end);
        const int finish = saved == saved_fields.end() ? end : std::clamp(saved->selection_end, start, end);
        focused_field->element->SetSelectionRange(start, finish);
        suppress_field_focus = false;
      }
      saved_fields.clear();
      restore_fields_pending = false;
    }
    if (view.skin_image) {
      skin->SetProperty("display", view.skin_visible ? "block" : "none");
      skin->SetProperty("z-index", view.skin_modal_input ? "3" : "1");
      skin->SetProperty("position", "absolute");
      skin->SetProperty("left", "0dp");
      skin->SetProperty("top", "0dp");
      skin->SetProperty("width", trimmed_number(view.width / scale) + "dp");
      skin->SetProperty("height", trimmed_number(view.height / scale) + "dp");
      ui::set_native_image(*skin, *view.skin_image, view.skin_revision, view.skin_dirty);
      ui::set_native_source(*skin, {0, 0, view.skin_image->width, view.skin_image->height});
    } else
      skin->SetProperty("display", "none");
    if (!view.sheet_dirty.empty()) startup.acknowledge_sheet_upload(view.sheet_revision);
    if (!view.skin_dirty.empty()) startup.acknowledge_skin_upload(view.skin_revision);
    revision_ = view.revision;
    sheet_revision_ = view.sheet_revision;
    skin_revision_ = view.skin_revision;
  }
  void sync_field(Rml::Element* element) {
    auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element);
    if (!input) return;
    const auto action = event_action(element);
    if (!action || action->kind != ActionKind::field) return;
    startup.set_field_value(action->index, input->GetValue());
    const auto accepted = startup.field_value(action->index);
    if (input->GetValue() != accepted) input->SetValue(accepted);
  }
  void sync_fields() {
    for (const auto& field : fields) sync_field(field.element);
  }
  void on_down(Rml::Event& event) {
    const auto action = event_action(event.GetTargetElement());
    if (startup.view().dialog && action && action->kind != ActionKind::cancel && action->kind != ActionKind::submit &&
        action->kind != ActionKind::link && action->kind != ActionKind::field &&
        action->kind != ActionKind::account_logout)
      return;
    if (action && acts_on_down(action->kind)) startup.perform(*action);
  }
  void on_click(Rml::Event& event) {
    const auto action = event_action(event.GetTargetElement());
    if (startup.view().dialog && action && action->kind != ActionKind::cancel && action->kind != ActionKind::submit &&
        action->kind != ActionKind::link && action->kind != ActionKind::field &&
        action->kind != ActionKind::account_logout)
      return;
    if (!action || acts_on_down(action->kind)) return;
    if (action->kind == ActionKind::submit) sync_fields();
    // SoundIcon plays no button_click.
    if (action->kind != ActionKind::sound) startup.play_click();
    startup.perform(*action);
  }
  // Screens whose pointer is handled by Startup keep the hover that Startup gave each element, whatever RML reports.
  [[nodiscard]] static bool tracks_hover(const StartupViewSnapshot& view) {
    return view.fame || view.legends || view.characters || view.classes;
  }
  void set_hover(const std::optional<StartupAction>& action) {
    hovered = action;
    const auto& view = startup.view();
    for (auto& node : nodes) {
      if (node.snapshot_index >= view.elements.size()) continue;
      const auto& item = view.elements[node.snapshot_index];
      const bool over =
          (tracks_hover(view) ? item.hovered : same_action(hovered, item.action)) && item.hover_color[3] > 0;
      node.hovered = over;
      set_node_color(node, item, view);
    }
  }
  [[nodiscard]] ui::Result handle(const ui::Event& event) {
    const auto& view = startup.view();
    if (!view.menus) return ui::Result::pass;
    if (event.kind == ui::EventKind::focus_lost || event.kind == ui::EventKind::pointer_left) {
      startup.cancel_pointer();
      if (view.editor) {
        EditorInput input;
        input.kind =
            event.kind == ui::EventKind::focus_lost ? EditorInput::Kind::focus_lost : EditorInput::Kind::pointer_left;
        (void)startup.editor_event(input);
      }
      host.layer().handle(event);
      return ui::Result::consumed;
    }
    if (view.editor) {
      const auto modifiers = event.modifiers;
      const bool in_editor = startup.editor_covers(event.point);
      switch (event.kind) {
        case ui::EventKind::pointer_move:
          if (startup.editor_captured() || in_editor) {
            EditorInput input;
            input.kind = EditorInput::Kind::pointer_move;
            input.point = event.point;
            input.shift = (modifiers & KMOD_SHIFT) != 0;
            input.control = (modifiers & KMOD_CTRL) != 0;
            (void)startup.editor_event(input);
            return ui::Result::consumed;
          }
          startup.pointer_move(event.point);
          return host.layer().handle(event);
        case ui::EventKind::pointer_down:
          if (!in_editor) {
            return host.layer().handle(event);
          } else {
            EditorInput input;
            input.kind = EditorInput::Kind::pointer_down;
            input.point = event.point;
            input.button = event.button;
            input.shift = (modifiers & KMOD_SHIFT) != 0;
            input.control = (modifiers & KMOD_CTRL) != 0;
            (void)startup.editor_event(input);
            return startup.editor_captured() ? ui::Result::capture : ui::Result::consumed;
          }
        case ui::EventKind::pointer_up:
          if (startup.editor_captured() || in_editor) {
            EditorInput input;
            input.kind = EditorInput::Kind::pointer_up;
            input.point = event.point;
            input.button = event.button;
            (void)startup.editor_event(input);
            return ui::Result::consumed;
          }
          return host.layer().handle(event);
        case ui::EventKind::wheel:
          if (in_editor) {
            EditorInput input;
            input.kind = EditorInput::Kind::wheel;
            input.point = event.point;
            input.wheel = event.wheel;
            (void)startup.editor_event(input);
            return ui::Result::consumed;
          }
          return host.layer().handle(event);
        case ui::EventKind::key_down:
        case ui::EventKind::key_up: {
          EditorInput input;
          input.kind = event.kind == ui::EventKind::key_down ? EditorInput::Kind::key_down : EditorInput::Kind::key_up;
          input.key = editor_key(event.key, modifiers);
          input.shift = (modifiers & KMOD_SHIFT) != 0;
          input.control = (modifiers & KMOD_CTRL) != 0;
          input.repeat = event.repeat;
          (void)startup.editor_event(input);
          if (input.key != EditorInputKey::none || startup.editor_wants_text()) return ui::Result::consumed;
          return host.layer().handle(event);
        }
        case ui::EventKind::text:
          if (startup.editor_wants_text()) {
            EditorInput input;
            input.kind = EditorInput::Kind::text;
            input.text = event.text;
            (void)startup.editor_event(input);
            return ui::Result::consumed;
          }
          return host.layer().handle(event);
        case ui::EventKind::composition:
          return startup.editor_wants_text() ? ui::Result::consumed : host.layer().handle(event);
        default:
          return ui::Result::pass;
      }
    }
    if (((view.characters || view.classes) && !view.dialog) || view.deletion || view.fame || view.legends ||
        view.skin_modal_input || (view.skin_visible && !view.dialog)) {
      switch (event.kind) {
        case ui::EventKind::pointer_move:
          startup.pointer_move(event.point);
          return ui::Result::consumed;
        case ui::EventKind::pointer_down:
          if (event.button == SDL_BUTTON_LEFT) {
            startup.pointer_down(event.point);
            return ui::Result::capture;
          }
          return ui::Result::consumed;
        case ui::EventKind::pointer_up:
          if (event.button == SDL_BUTTON_LEFT) startup.pointer_up(event.point);
          return ui::Result::consumed;
        case ui::EventKind::wheel:
          return startup.wheel(event.wheel) ? ui::Result::consumed : ui::Result::pass;
        case ui::EventKind::key_down:
          if (event.key == SDLK_ESCAPE) {
            startup.key(StartupKey::escape);
            return ui::Result::consumed;
          }
          if (event.key == SDLK_RETURN || event.key == SDLK_KP_ENTER) {
            sync_fields();
            startup.key(StartupKey::enter);
            return ui::Result::consumed;
          }
          if (event.key == SDLK_TAB)
            startup.key(StartupKey::tab);
          else if (event.key == SDLK_BACKSPACE)
            startup.key(StartupKey::backspace);
          return ui::Result::consumed;
        case ui::EventKind::text:
          startup.text(event.text);
          return ui::Result::consumed;
        case ui::EventKind::composition:
          return ui::Result::consumed;
        default:
          return ui::Result::consumed;
      }
    }
    switch (event.kind) {
      case ui::EventKind::wheel:
        if (startup.wheel(event.wheel)) return ui::Result::consumed;
        return host.layer().handle(event);
      case ui::EventKind::key_down:
        if (event.key == SDLK_ESCAPE) return startup.key(StartupKey::escape) ? ui::Result::consumed : ui::Result::pass;
        if ((event.key == SDLK_RETURN || event.key == SDLK_KP_ENTER) && view.dialog) {
          sync_fields();
          startup.key(StartupKey::enter);
          return ui::Result::consumed;
        }
        return host.layer().handle(event);
      case ui::EventKind::key_up:
        return host.layer().handle(event);
      case ui::EventKind::pointer_move:
        // The header's rank tooltip is hit-tested by the controller on every menu, while the DOM keeps hover styling.
        startup.pointer_move(event.point);
        return host.layer().handle(event);
      case ui::EventKind::text:
      case ui::EventKind::composition:
      case ui::EventKind::pointer_down:
      case ui::EventKind::pointer_up:
      case ui::EventKind::pointer_left:
      case ui::EventKind::focus_lost:
        return host.layer().handle(event);
      default:
        return ui::Result::pass;
    }
  }

  Startup& startup;
  ui::RmlHost& host;
  Rml::ElementDocument* document{};
  Rml::Element *screen{}, *skin{}, *dialog{};
  std::vector<Node> nodes;
  std::vector<FieldNode> fields;
  std::vector<NodeSignature> signature_;
  std::vector<SavedField> saved_fields;
  std::optional<StartupAction> hovered;
  std::uint64_t revision_{~std::uint64_t{}}, sheet_revision_{~std::uint64_t{}}, skin_revision_{~std::uint64_t{}};
  bool shown{}, modal{}, restore_fields_pending{}, suppress_field_focus{};
};

StartupRml::StartupRml(Startup& startup, ui::RmlHost& host) : impl_(std::make_unique<Impl>(startup, host)) {}
StartupRml::~StartupRml() = default;
void StartupRml::update() { impl_->update(); }
ui::Result StartupRml::handle(const ui::Event& event) { return impl_->handle(event); }
bool StartupRml::wants_text() const { return impl_->wants_text(); }
Rml::ElementDocument* StartupRml::document() const { return impl_->get_document(); }
}
