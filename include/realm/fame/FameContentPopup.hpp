#pragma once
#include "realm/lib/ui/RmlHost.hpp"
#include "realm/net/Http.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace Rml {
class ElementDocument;
}

namespace realm {
class Catalog;
class HudArt;

// The Deca "Fame Overview" popup (ui/fame.rml): the character's portrait, name, class, creation
// date and base fame, a Statistics tab (counters, then the death bonuses) and a Dungeons tab, and
// the fame on death. Flash computed the numbers from PCStats; here open() asks the server
// (/char/fameStats) so they follow the server's own bonus rules and only its dungeons are listed.
// Input reaches it through the host's layer while it is visible (the popup is modal).
class FameContentPopup {
 public:
  FameContentPopup(ui::RmlHost& host, const Catalog& catalog, const HudArt& art);
  ~FameContentPopup();
  FameContentPopup(const FameContentPopup&) = delete;
  FameContentPopup& operator=(const FameContentPopup&) = delete;

  // Shows the popup and requests the overview of `character_id`; a reply for an earlier open is
  // dropped.
  void open(std::string host, std::uint16_t port, net::FormFields credentials, int character_id);
  void close();
  [[nodiscard]] bool visible() const;
  // Once per frame before the host updates: takes a finished request and shows the document while
  // visible and `showable`.
  void update(bool showable);
  [[nodiscard]] Rml::ElementDocument* document() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
