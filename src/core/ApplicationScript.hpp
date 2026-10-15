#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace realm::application {
struct ScriptStep {
  double time{};
  std::string action;
  std::string argument;
  int x{};
  int y{};
};

std::vector<ScriptStep> load_script(const std::filesystem::path& path);
}
