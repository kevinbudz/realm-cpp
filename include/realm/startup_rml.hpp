#pragma once
#include "realm/startup.hpp"
#include "realm/ui_rml.hpp"
#include <memory>

namespace Rml {
class ElementDocument;
}

namespace realm {
// Incremental RmlUi view of Startup. The caller owns the shared RmlHost and routes
// startup-layer input through handle(); this object owns only its document.
class StartupRml {
 public:
  StartupRml(Startup& startup, ui::RmlHost& host);
  ~StartupRml();
  StartupRml(const StartupRml&) = delete;
  StartupRml& operator=(const StartupRml&) = delete;

  void update();
  [[nodiscard]] ui::Result handle(const ui::Event& event);
  [[nodiscard]] bool wants_text() const;
  [[nodiscard]] Rml::ElementDocument* document() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
