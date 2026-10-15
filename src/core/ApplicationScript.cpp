#include "ApplicationScript.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace realm::application {
std::vector<ScriptStep> load_script(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("Cannot read script " + path.string());
  }
  std::vector<ScriptStep> steps;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream input(line);
    ScriptStep step;
    if (!(input >> step.time >> step.action)) {
      throw std::invalid_argument("Invalid script line: " + line);
    }

    if (step.action == "click" || step.action == "move" || step.action == "resize" ||
        step.action == "down" || step.action == "up") {
      if (!(input >> step.x >> step.y)) {
        throw std::invalid_argument("Invalid pointer input: " + line);
      }
    } else if (step.action == "wheel") {
      if (!(input >> step.y)) {
        throw std::invalid_argument("Invalid wheel input: " + line);
      }
    } else if (step.action == "key" || step.action == "hold" || step.action == "release" ||
               step.action == "text" || step.action == "capture" || step.action == "compose") {
      std::getline(input >> std::ws, step.argument);
      if (step.argument.empty()) {
        throw std::invalid_argument("Invalid script argument: " + line);
      }
      // An empty composition (commit/clear) is written "-".
      if (step.action == "compose" && step.argument == "-") {
        step.argument.clear();
      }
    } else if (step.action != "quit" && step.action != "ability-down" &&
               step.action != "ability-up" && step.action != "focus-lost") {
      throw std::invalid_argument("Unknown script action: " + line);
    }

    steps.push_back(std::move(step));
  }

  std::stable_sort(steps.begin(), steps.end(),
                   [](const auto& a, const auto& b) { return a.time < b.time; });
  return steps;
}
}
