#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace realm::application {
struct ScriptStep {
  double time{};
  std::string action, argument;
  int x{}, y{};
};
std::vector<ScriptStep> load_script(const std::filesystem::path& path);
}
