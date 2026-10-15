#pragma once
// Flash: com/company/assembleegameclient/tutorial/Tutorial.as
#include "realm/map/World.hpp"
#include <filesystem>
#include <optional>

namespace realm {
struct TutorialStroke {
  std::vector<Vec2> points;
  double width{};
  std::uint32_t color{};
};

class Tutorial {
 public:
  Tutorial(const Catalog& catalog, const std::filesystem::path& flash_root);
  void reset();
  // Every matching proximity step draws simultaneously, as in Tutorial.onEnterFrame.
  [[nodiscard]] std::vector<TutorialStroke> update(const World& world, std::int32_t time);

 private:
  struct Drawing {
    bool arrow{};
    std::array<double, 4> coordinates{};
    std::uint32_t color{};
  };

  struct Requirement {
    int type{-1};
    std::string name;
    double radius{};
  };

  struct Step {
    std::vector<Requirement> requirements;
    std::vector<Drawing> drawings;
    std::optional<std::int32_t> satisfied_since;
  };

  std::vector<Step> steps_;
  std::uint64_t generation_{};
};
}
