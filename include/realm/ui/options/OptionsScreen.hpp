#pragma once
// Flash: com/company/assembleegameclient/ui/options/Options.as
#include "realm/lib/ui/RmlHost.hpp"
#include "realm/ui/options/OptionsMenu.hpp"
#include <memory>

namespace Rml {
class ElementDocument;
}

namespace realm {
// The RmlUi Settings screen (ui/options.rml) over an OptionsMenu: a sidebar with search,
// categories and the open category's sections, and rows of key boxes, switches, dropdowns
// and sliders. Clicks, drags and text reach it through the host's input layer; the caller
// takes the keys the menu wants first (see OptionsMenu::key_down).
class OptionsScreen {
 public:
  OptionsScreen(OptionsMenu& menu, ui::RmlHost& host);
  ~OptionsScreen();
  OptionsScreen(const OptionsScreen&) = delete;
  OptionsScreen& operator=(const OptionsScreen&) = delete;

  // The cursor sheet (4x4 cursors of 32 px) the Mouse Cursor row previews; without one the row
  // is a dropdown.
  void set_cursors(Image sheet);
  // Once per frame before the host updates. The document shows while `showable` (no startup screen
  // covers the game) and `opacity` is above 0; a closed menu keeps its last rows while it fades.
  void update(bool showable, double opacity);
  // The search field has keyboard focus.
  [[nodiscard]] bool wants_text() const;
  // Escape in the search field: clears the query, or with no query leaves the field.
  void leave_search();
  [[nodiscard]] Rml::ElementDocument* document() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
