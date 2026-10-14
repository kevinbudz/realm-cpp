#include "realm/tutorial/Tutorial.hpp"
#include <libxml/parser.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace realm {
Tutorial::Tutorial(const Catalog& catalog, const std::filesystem::path& root) {
  const auto path = root / "src/kabam/rotmg/assets/EmbeddedData_TutorialScriptCXML.dat";
  const auto document =
      std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>(xmlReadFile(path.c_str(), nullptr, XML_PARSE_NONET), xmlFreeDoc);
  if (!document) throw std::runtime_error("Cannot read tutorial script: " + path.string());
  for (const auto xml : Xml(xmlDocGetRootElement(document.get())).children("Step")) {
    Step step;
    for (const auto requirement : xml.children("Requirement")) {
      if (requirement.text() != "Near") throw std::runtime_error("Unsupported tutorial requirement");
      const auto* object = catalog.object(requirement.attribute("objectId"));
      step.requirements.push_back({object ? object->type : -1, requirement.attribute("objectName"),
                                   number(requirement.attribute("radius"), 1)});
    }
    for (const auto child : xml.children()) {
      if (child.name() != "UIDrawBox" && child.name() != "UIDrawArrow") continue;
      Drawing drawing;
      drawing.arrow = child.name() == "UIDrawArrow";
      drawing.color = static_cast<std::uint32_t>(number(child.attribute("color")));
      auto text = child.text();
      std::replace(text.begin(), text.end(), ',', ' ');
      std::istringstream input(text);
      for (auto& coordinate : drawing.coordinates)
        if (!(input >> coordinate) || !std::isfinite(coordinate))
          throw std::runtime_error("Invalid tutorial drawing coordinates");
      step.drawings.push_back(drawing);
    }
    steps_.push_back(std::move(step));
  }
}
void Tutorial::reset() {
  for (auto& step : steps_) step.satisfied_since.reset();
}
std::vector<TutorialStroke> Tutorial::update(const World& world, std::int32_t time) {
  if (generation_ != world.generation()) {
    generation_ = world.generation();
    reset();
  }
  std::vector<TutorialStroke> strokes;
  const auto* player = world.entity(world.player_id());
  if (world.name() != "Tutorial" || !player) {
    reset();
    return strokes;
  }
  const double pulse = std::abs(std::sin(time / 300.0));
  const double thickness = static_cast<int>(5 * pulse);
  for (auto& step : steps_) {
    const bool satisfied = std::all_of(step.requirements.begin(), step.requirements.end(), [&](const auto& req) {
      return std::any_of(world.entities().begin(), world.entities().end(), [&](const auto& entity) {
        return entity.type == req.type && (req.name.empty() || entity.profile.name == req.name) &&
               std::hypot(entity.motion.position.x - player->motion.position.x,
                          entity.motion.position.y - player->motion.position.y) <= req.radius;
      });
    });
    if (!satisfied) {
      step.satisfied_since.reset();
      continue;
    }
    if (!step.satisfied_since || time < *step.satisfied_since) step.satisfied_since = time;
    const double progress = std::clamp((time - *step.satisfied_since) / 500.0, 0.0, 1.0);
    for (const auto& drawing : step.drawings) {
      const auto& c = drawing.coordinates;
      TutorialStroke stroke;
      stroke.width = std::max(1.0, thickness);
      stroke.color = drawing.color;
      if (drawing.arrow) {
        const Vec2 tip{c[0] + (c[2] - c[0]) * progress, c[1] + (c[3] - c[1]) * progress};
        const double angle = std::atan2(c[1] - tip.y, c[0] - tip.x);
        const auto wing = [&](double offset) {
          return Vec2{tip.x + 30 * std::cos(angle + offset), tip.y + 30 * std::sin(angle + offset)};
        };
        stroke.points = {{c[0], c[1]}, tip, wing(std::numbers::pi / 8), tip, wing(-std::numbers::pi / 8)};
      } else {
        const double x = progress < 1 ? 250 + (c[0] - 250) * progress : c[0] + thickness / 2;
        const double y = progress < 1 ? 200 + (c[1] - 200) * progress : c[1] + thickness / 2;
        const double w = (c[2] - thickness) * progress, h = (c[3] - thickness) * progress;
        stroke.points = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, {x, y}};
      }
      strokes.push_back(std::move(stroke));
    }
  }
  return strokes;
}
}
