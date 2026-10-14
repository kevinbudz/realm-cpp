#include "realm/mapeditor/EditorScreen.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/objects/Catalog.hpp"
#include "realm/util/Image.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <utility>

namespace realm {
namespace {
constexpr double canvas_x = 144, canvas_y = 78, canvas_size = 512;
constexpr double palette_x = 660, palette_y = 78, palette_bottom = 590;
constexpr std::uint32_t panel = 0x222833, panel_dark = 0x151a21, line = 0x59616c, white = 0xf1f3f5;
bool inside(Vec2 p, double x, double y, double w, double h) {
  return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;
}
std::string folded(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
  return result;
}
std::uint32_t hash_color(std::string_view id, std::uint32_t base) {
  std::uint32_t hash = 2166136261U;
  for (const auto c : id) hash = (hash ^ static_cast<unsigned char>(c)) * 16777619U;
  const int r = static_cast<int>((base >> 16U) & 255U), g = static_cast<int>((base >> 8U) & 255U), b = base & 255U;
  const int shade = 74 + static_cast<int>((hash >> 8U) % 54U);
  return (static_cast<std::uint32_t>((r * shade / 128) & 255) << 16U) |
         (static_cast<std::uint32_t>((g * shade / 128) & 255) << 8U) |
         static_cast<std::uint32_t>((b * shade / 128) & 255);
}
void pop_utf8(std::string& value) {
  while (!value.empty()) {
    const auto byte = static_cast<unsigned char>(value.back());
    value.pop_back();
    if ((byte & 0xc0U) != 0x80U) break;
  }
}
std::string shortened(const FontBook& fonts, std::string_view value, int size, double width, bool bold = false) {
  std::string result(value);
  while (!result.empty() && fonts.measure(result, size, bold) > width) {
    pop_utf8(result);
  }
  if (result != value && width >= fonts.measure("...", size, bold)) result += "...";
  return result;
}
}  // namespace

EditorScreen::EditorScreen(const Catalog& catalog, const FontBook& fonts, const std::filesystem::path& flash_root,
                           const HudArt* art)
    : catalog_(catalog), fonts_(fonts), art_(art), document_(catalog, flash_root) {
  update_filtered();
}

void EditorScreen::resize(double width, double height, double pixel_scale) {
  const auto safe_scale = std::isfinite(pixel_scale) && pixel_scale > 0 ? pixel_scale : 1.0;
  if (drawable_width_ == width && drawable_height_ == height && scale_ == safe_scale) return;
  cancel_pointer();
  drawable_width_ = std::max(width, 1.0);
  drawable_height_ = std::max(height, 1.0);
  scale_ = safe_scale;
  mark_dirty();
}

Vec2 EditorScreen::logical(Vec2 drawable) const {
  const double layout_scale = drawable_height_ / 600.0;
  const double offset_x = (drawable_width_ - 800.0 * layout_scale) / 2.0;
  const double offset_y = (drawable_height_ - 600.0 * layout_scale) / 2.0;
  return {(drawable.x - offset_x) / layout_scale, (drawable.y - offset_y) / layout_scale};
}
bool EditorScreen::covers(Vec2 point) const {
  const auto p = logical(point);
  return p.x >= 0 && p.x < 800 && p.y >= 56 && p.y < 600;
}
bool EditorScreen::wants_text() const { return file_focus_ || search_focus_ || name_focus_; }
bool EditorScreen::in_canvas(Vec2 p) const { return inside(p, canvas_x, canvas_y, canvas_size, canvas_size); }
EditorScreen::Point EditorScreen::tile_at(Vec2 p) const {
  const double tile_size = 16 * zoom_;
  return {static_cast<int>(std::floor((p.x - (canvas_x + canvas_size / 2)) / tile_size + center_.x)),
          static_cast<int>(std::floor((p.y - (canvas_y + canvas_size / 2)) / tile_size + center_.y))};
}
void EditorScreen::mark_dirty() { dirty_ = true; }

std::uint32_t EditorScreen::choice_color(EditorLayer layer, const EditorChoice& choice) const {
  if (layer == EditorLayer::region && choice.color) return choice.color & 0xffffffU;
  if (layer == EditorLayer::object) {
    if (const auto* object = catalog_.object(choice.id)) return hash_color(choice.id, object->color);
    return hash_color(choice.id, 0xb87842);
  }
  return hash_color(choice.id, 0x4e7950);
}
const Image* EditorScreen::thumbnail(EditorLayer layer, int type) {
  if (!art_ || type < 0 || (layer != EditorLayer::ground && layer != EditorLayer::object)) return nullptr;
  const auto key =
      (static_cast<std::uint32_t>(layer == EditorLayer::object) << 16U) | static_cast<std::uint32_t>(type & 0xffff);
  if (const auto found = thumbnails_.find(key); found != thumbnails_.end()) return &found->second;
  const auto id = std::string(layer == EditorLayer::ground ? "editorground" : "editorobject") + std::to_string(type);
  if (!art_->contains(id)) return nullptr;
  if (thumbnails_.size() >= 128) thumbnails_.erase(thumbnails_.begin());
  return &thumbnails_.emplace(key, art_->image(id)).first->second;
}
void EditorScreen::blit_thumbnail(const Image& source, double x, double y, double size, double opacity, double clip_x,
                                  double clip_y, double clip_width, double clip_height) {
  if (size <= 0) return;
  auto scaled = source;
  scaled.pixel_scale = static_cast<double>(source.width) / size;
  ui_blit(image_, scaled, x + (size - source.width / scaled.pixel_scale) / 2,
          y + (size - source.height / scaled.pixel_scale) / 2, true, false, opacity, clip_x, clip_y, clip_width,
          clip_height);
}

void EditorScreen::update_filtered() {
  filtered_.clear();
  const auto needle = folded(filter_);
  const auto& choices = document_.choices(layer_);
  filtered_.reserve(choices.size());
  for (std::size_t i = 0; i < choices.size(); ++i)
    if (needle.empty() || folded(choices[i].id).find(needle) != std::string::npos) filtered_.push_back(i);
  if (selected_ >= choices.size()) selected_ = 0;
  if (!needle.empty() && std::find(filtered_.begin(), filtered_.end(), selected_) == filtered_.end())
    selected_ = filtered_.empty() ? 0 : filtered_.front();
  palette_row_ = std::min(palette_row_, filtered_.size() / 2);
  mark_dirty();
}
void EditorScreen::set_layer(EditorLayer layer) {
  if (layer_ == layer) return;
  layer_ = layer;
  selected_ = 0;
  palette_row_ = 0;
  filter_.clear();
  search_focus_ = false;
  update_filtered();
}
void EditorScreen::choose_tool(EditorTool tool) {
  tool_ = tool;
  status_ = tool == EditorTool::draw     ? "Draw: click or drag to paint tiles."
            : tool == EditorTool::erase  ? "Erase: click or drag to remove this layer."
            : tool == EditorTool::sample ? "Sample: click a tile to pick its current layer."
                                         : "Name: click an object tile, then type its name.";
  mark_dirty();
}

void EditorScreen::select_palette(Vec2 p) {
  if (inside(p, palette_x, 134, 136, palette_bottom - 134)) {
    const int col = static_cast<int>((p.x - palette_x) / 68);
    const int row = static_cast<int>((p.y - 134) / 40);
    const auto at = (palette_row_ + static_cast<std::size_t>(row)) * 2 + static_cast<std::size_t>(col);
    if (at < filtered_.size()) {
      selected_ = filtered_[at];
      status_ = document_.choices(layer_)[selected_].id;
      mark_dirty();
    }
  }
}

void EditorScreen::activate_button(Vec2 p) {
  cancel_pointer();
  if (inside(p, 12, 78, 120, 28)) return choose_tool(EditorTool::draw);
  if (inside(p, 12, 109, 120, 28)) return choose_tool(EditorTool::erase);
  if (inside(p, 12, 140, 120, 28)) return choose_tool(EditorTool::sample);
  if (inside(p, 12, 171, 120, 28)) return choose_tool(EditorTool::name);
  if (inside(p, 12, 211, 56, 28)) {
    status_ = document_.undo() ? "Undo." : "Nothing to undo.";
    return mark_dirty();
  }
  if (inside(p, 76, 211, 56, 28)) {
    status_ = document_.redo() ? "Redo." : "Nothing to redo.";
    return mark_dirty();
  }
  if (inside(p, 12, 252, 120, 28)) {
    document_.clear();
    status_ = "Map cleared. Undo can restore it.";
    return mark_dirty();
  }
  if (inside(p, 12, 293, 120, 28)) {
    file_mode_ = FileMode::load;
    file_input_ = file_path_.string();
    file_focus_ = true;
    status_ = "Enter the .jm map path to load.";
    return mark_dirty();
  }
  if (inside(p, 12, 324, 120, 28)) {
    file_mode_ = FileMode::save;
    file_input_ = file_path_.string();
    file_focus_ = true;
    status_ = "Enter a .jm map path to save.";
    return mark_dirty();
  }
  if (inside(p, 12, 365, 120, 32)) {
    test_request_ = true;
    return;
  }
  if (inside(p, 12, 406, 120, 28)) {
    exit_request_ = true;
    return;
  }
}

void EditorScreen::pointer_down(Vec2 p, int button, bool shift, bool control) {
  pointer_ = p;
  pointer_valid_ = true;
  if (file_mode_ != FileMode::none) {
    if (inside(p, 214, 260, 372, 36)) {
      file_focus_ = true;
      return mark_dirty();
    }
    if (inside(p, 214, 322, 116, 30)) return complete_file_dialog();
    if (inside(p, 346, 322, 116, 30)) {
      file_mode_ = FileMode::none;
      file_focus_ = false;
      status_ = "File operation cancelled.";
      return mark_dirty();
    }
    file_focus_ = false;
    mark_dirty();
    return;
  }
  if (name_focus_) close_name(true);
  if (inside(p, 660, 78, 136, 26)) {
    const int tab = std::clamp(static_cast<int>((p.x - palette_x) / 45.34), 0, 2);
    set_layer(static_cast<EditorLayer>(tab));
    return;
  }
  if (inside(p, 660, 106, 136, 24)) {
    search_focus_ = true;
    file_focus_ = false;
    mark_dirty();
    return;
  }
  if (inside(p, 12, 72, 120, 450)) return activate_button(p);
  if (inside(p, 660, 134, 136, palette_bottom - 134)) return select_palette(p);
  if (!in_canvas(p)) return;
  if (document_.choices(layer_).empty()) {
    status_ = "No palette choices are available on this layer.";
    return mark_dirty();
  }
  if (tool_ == EditorTool::draw && button != 3 && filtered_.empty()) {
    status_ = "No palette choices match the filter.";
    return mark_dirty();
  }
  const auto cell = tile_at(p);
  if (cell.x < 0 || cell.y < 0 || cell.x >= MapDocument::extent || cell.y >= MapDocument::extent) {
    status_ = "Pan back inside the 256 by 256 map to edit tiles.";
    return mark_dirty();
  }
  if (button == 2 || control) {
    drag_ = Drag::pan;
    drag_pointer_ = p;
    pan_center_ = center_;
    return;
  }
  if (tool_ == EditorTool::sample) {
    if (cell.x >= 0 && cell.y >= 0 && cell.x < MapDocument::extent && cell.y < MapDocument::extent) {
      const auto& id = document_.tile(cell.x, cell.y).ids[static_cast<std::size_t>(layer_)];
      const auto& choices = document_.choices(layer_);
      const auto found =
          std::find_if(choices.begin(), choices.end(), [&](const auto& choice) { return choice.id == id; });
      if (!id.empty() && found != choices.end()) {
        selected_ = static_cast<std::size_t>(found - choices.begin());
        status_ = "Sampled " + id + ".";
      } else
        status_ = "This tile has no item on the selected layer.";
    }
    mark_dirty();
    return;
  }
  if (tool_ == EditorTool::name) {
    if (cell.x < 0 || cell.y < 0 || cell.x >= MapDocument::extent || cell.y >= MapDocument::extent ||
        document_.tile(cell.x, cell.y).ids[1].empty()) {
      status_ = "Place an object on this tile before naming it.";
      mark_dirty();
      return;
    }
    name_tile_ = cell;
    name_before_ = document_.tile(cell.x, cell.y).name;
    document_.begin_edit();
    name_focus_ = true;
    status_ = "Type the object name; Enter commits, Escape cancels.";
    mark_dirty();
    return;
  }
  drag_start_ = drag_last_ = cell;
  shift_rect_ = shift || shift_down_;
  drag_ = shift_rect_ ? Drag::rectangle : Drag::paint;
  drag_erase_ = button == 3 || tool_ == EditorTool::erase;
  document_.begin_edit();
  document_.paint(cell.x, cell.y, layer_, drag_erase_ ? std::string{} : document_.choices(layer_).at(selected_).id);
  mark_dirty();
}

void EditorScreen::pointer_move(Vec2 p, bool shift, bool control) {
  pointer_ = p;
  pointer_valid_ = true;
  shift_down_ = shift;
  control_down_ = control;
  hover_choice_.clear();
  hover_tile_.clear();
  if (drag_ == Drag::pan) {
    const double tile_size = 16 * zoom_;
    center_ = {pan_center_.x - (p.x - drag_pointer_.x) / tile_size,
               pan_center_.y - (p.y - drag_pointer_.y) / tile_size};
    mark_dirty();
    return;
  }
  if (drag_ == Drag::paint && in_canvas(p)) {
    const auto cell = tile_at(p);
    if (cell.x != drag_last_.x || cell.y != drag_last_.y) {
      drag_last_ = cell;
      if (cell.x < 0 || cell.y < 0 || cell.x >= MapDocument::extent || cell.y >= MapDocument::extent) return;
      document_.paint(cell.x, cell.y, layer_, drag_erase_ ? std::string{} : document_.choices(layer_).at(selected_).id);
      mark_dirty();
    }
    return;
  }
  if (drag_ == Drag::rectangle) {
    drag_last_ = tile_at(p);
    mark_dirty();
    return;
  }
  if (in_canvas(p)) {
    const auto cell = tile_at(p);
    if (cell.x >= 0 && cell.y >= 0 && cell.x < MapDocument::extent && cell.y < MapDocument::extent) {
      const auto& tile = document_.tile(cell.x, cell.y);
      hover_tile_ = std::to_string(cell.x) + ", " + std::to_string(cell.y) + "  ";
      const auto& id = tile.ids[static_cast<std::size_t>(layer_)];
      hover_tile_ += id.empty() ? "(empty)" : id;
      if (!tile.name.empty()) hover_tile_ += "  \"" + tile.name + "\"";
    }
  } else if (inside(p, palette_x, 134, 136, palette_bottom - 134)) {
    const int col = static_cast<int>((p.x - palette_x) / 68);
    const int row = static_cast<int>((p.y - 134) / 40);
    const auto at = (palette_row_ + static_cast<std::size_t>(row)) * 2 + static_cast<std::size_t>(col);
    if (at < filtered_.size()) {
      const auto& choice = document_.choices(layer_)[filtered_[at]];
      hover_choice_ = choice.id + "  (#" + std::to_string(choice.type) + ")";
    }
  }
  mark_dirty();
}

void EditorScreen::pointer_up(Vec2 p) {
  if (drag_ == Drag::paint) document_.end_edit();
  if (drag_ == Drag::rectangle) {
    const auto last = tile_at(p);
    const int left = std::max(0, std::min(drag_start_.x, last.x));
    const int right = std::min(MapDocument::extent - 1, std::max(drag_start_.x, last.x));
    const int top = std::max(0, std::min(drag_start_.y, last.y));
    const int bottom = std::min(MapDocument::extent - 1, std::max(drag_start_.y, last.y));
    const std::string id = drag_erase_ ? std::string{} : document_.choices(layer_).at(selected_).id;
    for (int y = top; y <= bottom; ++y)
      for (int x = left; x <= right; ++x)
        if (x >= 0 && y >= 0 && x < MapDocument::extent && y < MapDocument::extent) document_.paint(x, y, layer_, id);
    document_.end_edit();
  }
  drag_ = Drag::none;
  drag_erase_ = false;
  shift_rect_ = false;
  mark_dirty();
}

void EditorScreen::wheel(Vec2 p, double steps) {
  if (file_mode_ != FileMode::none) return;
  cancel_pointer();
  if (inside(p, 660, 134, 136, palette_bottom - 134)) {
    const auto rows = (filtered_.size() + 1) / 2;
    const auto visible = static_cast<std::size_t>((palette_bottom - 134) / 40);
    const auto maximum = rows > visible ? rows - visible : 0;
    const auto delta = static_cast<long>(std::lround(steps * 3));
    const auto next = static_cast<long>(palette_row_) - delta;
    palette_row_ = static_cast<std::size_t>(std::clamp(next, 0L, static_cast<long>(maximum)));
    return mark_dirty();
  }
  if (!in_canvas(p)) return;
  const double old = zoom_;
  const double factor = std::pow(1.2, std::clamp(steps, -10.0, 10.0));
  zoom_ = std::clamp(zoom_ * factor, 0.125, 16.0);
  if (zoom_ == old) return;
  const auto anchor_x = (p.x - (canvas_x + canvas_size / 2)) / (16 * old) + center_.x;
  const auto anchor_y = (p.y - (canvas_y + canvas_size / 2)) / (16 * old) + center_.y;
  center_.x = anchor_x - (p.x - (canvas_x + canvas_size / 2)) / (16 * zoom_);
  center_.y = anchor_y - (p.y - (canvas_y + canvas_size / 2)) / (16 * zoom_);
  mark_dirty();
}

void EditorScreen::close_name(bool commit) {
  if (!name_focus_) return;
  if (!commit) document_.name(name_tile_.x, name_tile_.y, name_before_);
  document_.end_edit();
  name_focus_ = false;
  status_ = commit ? "Object name saved." : "Object name cancelled.";
  mark_dirty();
}

void EditorScreen::complete_file_dialog() {
  if (file_mode_ == FileMode::none) return;
  try {
    const std::filesystem::path path(file_input_);
    if (path.empty()) throw std::runtime_error("Enter a map path first.");
    if (file_mode_ == FileMode::load) {
      document_.load_file(path);
      status_ = "Loaded " + path.string() + ".";
    } else {
      document_.save_file(path);
      status_ = "Saved " + path.string() + ".";
    }
    file_path_ = path;
    file_mode_ = FileMode::none;
    file_focus_ = false;
    mark_dirty();
  } catch (const std::exception& error) {
    status_ = error.what();
    mark_dirty();
  }
}

void EditorScreen::key(EditorInputKey key, bool down, bool repeat) {
  if (key == EditorInputKey::shift) shift_down_ = down;
  if (key == EditorInputKey::control) control_down_ = down;
  if (!down || repeat) return;
  if (key == EditorInputKey::escape) {
    if (file_mode_ != FileMode::none) {
      file_mode_ = FileMode::none;
      file_focus_ = false;
      status_ = "File operation cancelled.";
      mark_dirty();
    } else if (name_focus_)
      close_name(false);
    else if (search_focus_) {
      search_focus_ = false;
      mark_dirty();
    } else {
      cancel_pointer();
      exit_request_ = true;
    }
    return;
  }
  if (file_mode_ != FileMode::none) {
    if (file_focus_ && key == EditorInputKey::enter)
      complete_file_dialog();
    else if (file_focus_ && key == EditorInputKey::backspace && !file_input_.empty()) {
      pop_utf8(file_input_);
      mark_dirty();
    }
    return;
  }
  if (name_focus_) {
    if (key == EditorInputKey::enter)
      close_name(true);
    else if (key == EditorInputKey::backspace)
      append_text("\b");
    return;
  }
  if (search_focus_) {
    if (key == EditorInputKey::enter || key == EditorInputKey::tab) {
      search_focus_ = false;
      mark_dirty();
    } else if (key == EditorInputKey::backspace)
      append_text("\b");
    return;
  }
  if (key != EditorInputKey::none && key != EditorInputKey::shift && key != EditorInputKey::control) cancel_pointer();
  switch (key) {
    case EditorInputKey::enter:
    case EditorInputKey::tab:
      set_layer(static_cast<EditorLayer>((static_cast<int>(layer_) + 1) % 3));
      break;
    case EditorInputKey::left:
      center_.x -= 1;
      mark_dirty();
      break;
    case EditorInputKey::right:
      center_.x += 1;
      mark_dirty();
      break;
    case EditorInputKey::up:
      center_.y -= 1;
      mark_dirty();
      break;
    case EditorInputKey::down:
      center_.y += 1;
      mark_dirty();
      break;
    case EditorInputKey::undo:
      status_ = document_.undo() ? "Undo." : "Nothing to undo.";
      mark_dirty();
      break;
    case EditorInputKey::redo:
      status_ = document_.redo() ? "Redo." : "Nothing to redo.";
      mark_dirty();
      break;
    case EditorInputKey::draw:
      choose_tool(EditorTool::draw);
      break;
    case EditorInputKey::erase:
      choose_tool(EditorTool::erase);
      break;
    case EditorInputKey::sample:
      choose_tool(EditorTool::sample);
      break;
    case EditorInputKey::name:
      choose_tool(EditorTool::name);
      break;
    case EditorInputKey::clear:
      document_.clear();
      status_ = "Map cleared. Undo can restore it.";
      mark_dirty();
      break;
    case EditorInputKey::load:
      file_mode_ = FileMode::load;
      file_input_ = file_path_.string();
      file_focus_ = true;
      mark_dirty();
      break;
    case EditorInputKey::save:
      file_mode_ = FileMode::save;
      file_input_ = file_path_.string();
      file_focus_ = true;
      mark_dirty();
      break;
    case EditorInputKey::test:
      test_request_ = true;
      break;
    case EditorInputKey::back:
      cancel_pointer();
      exit_request_ = true;
      break;
    case EditorInputKey::zoom_in:
      wheel({canvas_x + canvas_size / 2, canvas_y + canvas_size / 2}, 1);
      break;
    case EditorInputKey::zoom_out:
      wheel({canvas_x + canvas_size / 2, canvas_y + canvas_size / 2}, -1);
      break;
    default:
      break;
  }
}

void EditorScreen::append_text(std::string_view value) {
  if (value == "\b") {
    std::string* destination = file_focus_ ? &file_input_ : search_focus_ ? &filter_ : nullptr;
    if (!destination) {
      if (name_focus_) {
        auto current = document_.tile(name_tile_.x, name_tile_.y).name;
        if (!current.empty()) {
          pop_utf8(current);
          document_.name(name_tile_.x, name_tile_.y, current);
          mark_dirty();
        }
      }
      return;
    }
    if (!destination->empty()) {
      pop_utf8(*destination);
      if (search_focus_)
        update_filtered();
      else
        mark_dirty();
    }
    return;
  }
  if (file_focus_) {
    if (file_input_.size() + value.size() <= 512) file_input_.append(value);
    mark_dirty();
  } else if (search_focus_) {
    if (filter_.size() + value.size() <= 64) filter_.append(value);
    update_filtered();
  } else if (name_focus_) {
    auto current = document_.tile(name_tile_.x, name_tile_.y).name;
    if (current.size() + value.size() <= 64) document_.name(name_tile_.x, name_tile_.y, current + std::string(value));
    mark_dirty();
  }
}

void EditorScreen::cancel_pointer() {
  if (drag_ == Drag::paint || drag_ == Drag::rectangle) document_.end_edit();
  drag_ = Drag::none;
  drag_erase_ = false;
  shift_rect_ = false;
  pointer_valid_ = false;
  mark_dirty();
}

bool EditorScreen::take_test_request() { return std::exchange(test_request_, false); }
bool EditorScreen::take_exit_request() { return std::exchange(exit_request_, false); }

void EditorScreen::handle(const EditorInput& event) {
  switch (event.kind) {
    case EditorInput::Kind::pointer_move:
      pointer_move(logical(event.point), event.shift, event.control);
      break;
    case EditorInput::Kind::pointer_down:
      pointer_down(logical(event.point), event.button, event.shift, event.control);
      break;
    case EditorInput::Kind::pointer_up:
      pointer_up(logical(event.point));
      break;
    case EditorInput::Kind::wheel:
      wheel(logical(event.point), event.wheel);
      break;
    case EditorInput::Kind::key_down:
      key(event.key, true, event.repeat);
      break;
    case EditorInput::Kind::key_up:
      key(event.key, false, event.repeat);
      break;
    case EditorInput::Kind::text:
      append_text(event.text);
      break;
    case EditorInput::Kind::pointer_left:
      cancel_pointer();
      break;
    case EditorInput::Kind::focus_lost:
      // A lost window focus must close the name transaction so later edits
      // remain undoable and the model does not stay in edit mode.
      if (name_focus_) close_name(true);
      cancel_pointer();
      shift_down_ = control_down_ = false;
      file_focus_ = search_focus_ = false;
      break;
  }
}

void EditorScreen::fill(double x, double y, double width, double height, std::uint32_t color) {
  ui_rectangle(image_, x, y, width, height, color);
}
void EditorScreen::outline(double x, double y, double width, double height, std::uint32_t color) {
  fill(x, y, width, 1, color);
  fill(x, y + height - 1, width, 1, color);
  fill(x, y, 1, height, color);
  fill(x + width - 1, y, 1, height, color);
}
void EditorScreen::text(std::string_view value, int size, std::uint32_t color, double x, double y, bool bold) {
  const auto bitmap = fonts_.rasterize_at_scale(value, size, scale_, bold, false, color);
  ui_blit_text(image_, bitmap, x, y);
}

void EditorScreen::render_canvas() {
  fill(canvas_x, canvas_y, canvas_size, canvas_size, 0x151a20);
  const double tile_size = 16 * zoom_, screen_x = canvas_x + canvas_size / 2, screen_y = canvas_y + canvas_size / 2;
  const int left = std::max(0, static_cast<int>(std::floor(center_.x - canvas_size / (2 * tile_size))) - 1);
  const int top = std::max(0, static_cast<int>(std::floor(center_.y - canvas_size / (2 * tile_size))) - 1);
  const int right =
      std::min(MapDocument::extent, static_cast<int>(std::ceil(center_.x + canvas_size / (2 * tile_size))) + 1);
  const int bottom =
      std::min(MapDocument::extent, static_cast<int>(std::ceil(center_.y + canvas_size / (2 * tile_size))) + 1);
  const auto tile_color = [&](EditorLayer layer, std::string_view id) {
    if (id.empty()) return std::uint32_t{};
    if (layer == EditorLayer::ground) return hash_color(id, 0x4e7950);
    if (layer == EditorLayer::object) {
      if (const auto* object = catalog_.object(id)) return hash_color(id, object->color);
      return hash_color(id, 0xb87842);
    }
    const auto& choices = document_.choices(layer);
    const auto found =
        std::find_if(choices.begin(), choices.end(), [&](const auto& choice) { return choice.id == id; });
    return found == choices.end() ? hash_color(id, 0x7d5988) : choice_color(layer, *found);
  };
  for (int y = top; y < bottom; ++y)
    for (int x = left; x < right; ++x) {
      const double sx = screen_x + (x - center_.x) * tile_size;
      const double sy = screen_y + (y - center_.y) * tile_size;
      const double rx = std::max(sx, canvas_x), ry = std::max(sy, canvas_y);
      const double rw = std::min(sx + tile_size, canvas_x + canvas_size) - rx;
      const double rh = std::min(sy + tile_size, canvas_y + canvas_size) - ry;
      if (rw <= 0 || rh <= 0) continue;
      const auto& tile = document_.tile(x, y);
      const auto ground = tile_color(EditorLayer::ground, tile.ids[0]);
      fill(rx, ry, rw, rh, ground ? ground : 0x242b32);
      if (!tile.ids[0].empty() && tile_size >= 4) {
        if (const auto* ground_def = catalog_.ground(tile.ids[0]))
          if (const auto* sprite = thumbnail(EditorLayer::ground, ground_def->type))
            blit_thumbnail(*sprite, sx, sy, tile_size, 1, canvas_x, canvas_y, canvas_size, canvas_size);
      }
      if (!tile.ids[2].empty())
        outline(rx + 1, ry + 1, std::max(1.0, rw - 2), std::max(1.0, rh - 2),
                tile_color(EditorLayer::region, tile.ids[2]));
      if (!tile.ids[1].empty()) {
        const double margin = std::max(1.0, tile_size * .27);
        const auto icon_size = std::max(1.0, tile_size - margin * 2);
        if (const auto* object_def = catalog_.object(tile.ids[1])) {
          if (const auto* sprite = thumbnail(EditorLayer::object, object_def->type); sprite && tile_size >= 4)
            blit_thumbnail(*sprite, sx + margin, sy + margin, icon_size, 1, canvas_x, canvas_y, canvas_size,
                           canvas_size);
          else
            fill(sx + margin, sy + margin, icon_size, icon_size, tile_color(EditorLayer::object, tile.ids[1]));
        }
      }
      if (tile_size >= 8) outline(rx, ry, rw, rh, 0x343b43);
      if (tile_size >= 54) {
        const auto& id = tile.ids[static_cast<std::size_t>(layer_)];
        const auto label = shortened(fonts_, id.empty() ? std::string_view{} : std::string_view(id), 9, tile_size - 4);
        if (!label.empty()) text(label, 9, white, sx + 2, sy + 2);
      }
    }
  // The map border is visible as the camera pans off the 256 by 256 document.
  const double edge_left = screen_x + (0 - center_.x) * tile_size;
  const double edge_top = screen_y + (0 - center_.y) * tile_size;
  const double edge_right = screen_x + (MapDocument::extent - center_.x) * tile_size;
  const double edge_bottom = screen_y + (MapDocument::extent - center_.y) * tile_size;
  if (edge_left >= canvas_x && edge_left <= canvas_x + canvas_size) fill(edge_left, canvas_y, 1, canvas_size, 0xb7c5d4);
  if (edge_top >= canvas_y && edge_top <= canvas_y + canvas_size) fill(canvas_x, edge_top, canvas_size, 1, 0xb7c5d4);
  if (edge_right >= canvas_x && edge_right <= canvas_x + canvas_size)
    fill(edge_right, canvas_y, 1, canvas_size, 0xb7c5d4);
  if (edge_bottom >= canvas_y && edge_bottom <= canvas_y + canvas_size)
    fill(canvas_x, edge_bottom, canvas_size, 1, 0xb7c5d4);
  if (drag_ == Drag::rectangle) {
    const int l = std::min(drag_start_.x, drag_last_.x), r = std::max(drag_start_.x, drag_last_.x) + 1;
    const int t = std::min(drag_start_.y, drag_last_.y), b = std::max(drag_start_.y, drag_last_.y) + 1;
    const double x = screen_x + (l - center_.x) * tile_size, y = screen_y + (t - center_.y) * tile_size;
    outline(x, y, (r - l) * tile_size, (b - t) * tile_size, 0xffffff);
  }
  outline(canvas_x, canvas_y, canvas_size, canvas_size, 0x9aa7b3);
}

void EditorScreen::render_toolbar() {
  fill(12, 72, 120, 450, 0x1a2028);
  outline(12, 72, 120, 450, line);
  const auto button = [&](double x, double y, double w, std::string_view label, bool active = false) {
    fill(x, y, w, 28, active ? 0x566475 : panel);
    outline(x, y, w, 28, active ? 0xc7d3e0 : line);
    text(label, 13, white, x + 7, y + 5, active);
  };
  button(12, 78, 120, "D  Draw", tool_ == EditorTool::draw);
  button(12, 109, 120, "E  Erase", tool_ == EditorTool::erase);
  button(12, 140, 120, "A  Sample", tool_ == EditorTool::sample);
  button(12, 171, 120, "I  Name", tool_ == EditorTool::name);
  button(12, 211, 56, "U  Undo");
  button(76, 211, 56, "R  Redo");
  button(12, 252, 120, "C  Clear");
  button(12, 293, 120, "L  Load");
  button(12, 324, 120, "S  Save");
  button(12, 365, 120, "T  Test", false);
  button(12, 406, 120, "Esc  Back");
  text("Layer", 11, 0xb7c0ca, 18, 444);
  text(layer_ == EditorLayer::ground   ? "Ground"
       : layer_ == EditorLayer::object ? "Object"
                                       : "Region",
       13, white, 18, 461, true);
  text("Tile", 11, 0xb7c0ca, 18, 485);
  text(hover_tile_.empty() ? "Move over map" : shortened(fonts_, hover_tile_, 10, 108), 10, white, 18, 501);
  const auto lines = fonts_.wrap(status_, 10, false, 110);
  for (std::size_t i = 0; i < std::min<std::size_t>(3, lines.size()); ++i)
    text(lines[i], 10, status_.find("error") != std::string::npos ? 0xff9a89 : 0xc3cbd3, 18,
         536 + static_cast<double>(i) * 14);
}

void EditorScreen::render_palette() {
  fill(palette_x, palette_y, 136, palette_bottom - palette_y, 0x1b2028);
  outline(palette_x, palette_y, 136, palette_bottom - palette_y, line);
  constexpr std::string_view layers[]{"Ground", "Object", "Region"};
  for (int i = 0; i < 3; ++i) {
    const double x = palette_x + i * 45.34;
    fill(x, palette_y, 45.34, 26, static_cast<int>(layer_) == i ? 0x58697b : panel);
    text(layers[i], 9, white, x + 3, palette_y + 7, static_cast<int>(layer_) == i);
  }
  fill(palette_x + 2, 106, 132, 24, panel_dark);
  outline(palette_x + 2, 106, 132, 24, search_focus_ ? 0xd5a65a : line);
  text(search_focus_ ? "> " + filter_ : filter_.empty() ? "Filter palette..." : filter_, 10, white, palette_x + 7, 111);
  const auto rows = static_cast<std::size_t>((palette_bottom - 134) / 40);
  for (std::size_t row = 0; row < rows; ++row)
    for (std::size_t col = 0; col < 2; ++col) {
      const auto at = (palette_row_ + row) * 2 + col;
      if (at >= filtered_.size()) continue;
      const auto choice_index = filtered_[at];
      const auto& choice = document_.choices(layer_)[choice_index];
      const double x = palette_x + static_cast<double>(col) * 68, y = 134 + static_cast<double>(row) * 40;
      const bool selected = choice_index == selected_;
      fill(x + 1, y + 1, 66, 38, selected ? 0x394451 : ((row + col) % 2 ? 0x292f38 : 0x242a33));
      if (selected) outline(x + 1, y + 1, 66, 38, 0xd5a65a);
      if (const auto* sprite = thumbnail(layer_, choice.type); sprite && (layer_ != EditorLayer::region))
        blit_thumbnail(*sprite, x + 4, y + 4, 28);
      else
        fill(x + 5, y + 7, 12, 12, choice_color(layer_, choice));
      text(shortened(fonts_, choice.id, 8, 34), 8, selected ? white : 0xd2d6db, x + 34, y + 6, selected);
      text("#" + std::to_string(choice.type), 8, 0x909aa5, x + 34, y + 21);
    }
  if (!hover_choice_.empty() && pointer_valid_) {
    const auto p = pointer_;
    const auto label = shortened(fonts_, hover_choice_, 11, 240);
    const auto width = std::min(250.0, fonts_.measure(label, 11, false) + 14);
    const double x = std::clamp(p.x - width - 8, 4.0, 796.0 - width), y = std::clamp(p.y + 8, 58.0, 570.0);
    fill(x, y, width, 24, 0x090c10);
    outline(x, y, width, 24, 0x9ba8b4);
    text(label, 11, white, x + 7, y + 5);
  }
}

void EditorScreen::render_dialog() {
  if (file_mode_ == FileMode::none) return;
  fill(0, 56, 800, 544, 0x090c10);
  fill(200, 214, 400, 176, 0x262c35);
  outline(200, 214, 400, 176, 0xd5a65a);
  text(file_mode_ == FileMode::load ? "Load map" : "Save map", 20, white, 220, 228, true);
  text("Map file path", 12, 0xc2c9d1, 220, 253);
  fill(214, 260, 372, 36, panel_dark);
  outline(214, 260, 372, 36, file_focus_ ? 0xd5a65a : line);
  const auto shown = file_input_.size() > 48 ? "..." + file_input_.substr(file_input_.size() - 45) : file_input_;
  text((file_focus_ ? "> " : "") + shown, 13, white, 222, 270);
  if (!status_.empty()) text(shortened(fonts_, status_, 10, 370), 10, 0xffc2a1, 218, 300);
  fill(214, 322, 116, 30, 0x58697b);
  fill(346, 322, 116, 30, panel);
  outline(214, 322, 116, 30, line);
  outline(346, 322, 116, 30, line);
  text(file_mode_ == FileMode::load ? "Load" : "Save", 13, white, 258, 330, true);
  text("Cancel", 13, white, 380, 330);
  text("Enter confirms  •  Escape cancels", 10, 0xaab2bb, 222, 364);
}

void EditorScreen::render() {
  image_ = ui_image(800, 600, scale_);
  render_canvas();
  render_toolbar();
  render_palette();
  // The account header occupies the first 30 px; keep this title below it.
  text("MAP EDITOR", 16, 0xd2d8df, 352, 39, true);
  text("256 × 256  |  Shift: rectangle  Ctrl-drag: pan  Wheel: zoom", 10, 0x9da7b1, 263, 58);
  render_dialog();
  dirty_ = false;
  ++image_revision_;
}
const Image& EditorScreen::image() {
  if (dirty_) render();
  return image_;
}

}  // namespace realm
