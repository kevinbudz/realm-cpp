#include "realm/tutorial/Tutorial.hpp"
#include "realm/tutorial/TutorialView.hpp"
#include "flash_assets.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
namespace {
void check(bool value, const char* reason) {
  if (!value) throw std::runtime_error(reason);
}
}
int main() {
  try {
    using namespace realm;
    Catalog catalog(flash::source_root);
    Tutorial tutorial(catalog, flash::source_root);
    World world(catalog);
    world.initialize(20, 20, "Tutorial");
    world.add(1, catalog.object("Wizard")->type, {5, 5});
    world.set_player_id(1);
    auto& sign = world.add(2, catalog.object("Sign")->type, {6, 5});
    sign.profile.name = "Check out the|minimap.";
    auto strokes = tutorial.update(world, 0);
    check(strokes.size() == 2, "near matching sign activates minimap box and arrow");
    check(strokes[0].points[0].x == 250 && strokes[0].points[0].y == 200,
          "box animates from Flash origin even when satisfaction begins at zero time");
    strokes = tutorial.update(world, 600);
    check(strokes[0].points[0].x >= 600 && strokes[0].points[1].x <= 800, "settled minimap box covers right HUD");
    const auto image = render_tutorial(strokes, 600);
    check(image.width == 800 && image.height == 600, "tutorial raster uses stage coordinates");
    check(std::any_of(image.pixels.begin(), image.pixels.end(), [](auto byte) { return byte != 0; }),
          "overlay raster contains visible highlights");
    bool translucent{};
    for (std::size_t i = 0; i < image.pixels.size(); i += 4)
      if (image.pixels[i + 3] > 0 && image.pixels[i + 3] < 255) {
        translucent = true;
        check(image.pixels[i + 1] > 200, "tutorial glow retains straight-alpha green for UI upload");
      }
    check(translucent, "tutorial glow has translucent edges");
    world.entity(2)->profile.name = "Other sign";
    check(tutorial.update(world, 700).empty(), "sign text is part of the proximity requirement");
    world.entity(2)->profile.name = "Check out the|minimap.";
    world.entity(1)->motion.position = {12, 12};
    check(tutorial.update(world, 800).empty(), "leaving radius hides overlay");
    world.entity(1)->motion.position = {5, 5};
    strokes = tutorial.update(world, 900);
    check(strokes[0].points[0].x == 250, "reentering radius restarts animation");
    world.initialize(20, 20, "Nexus");
    check(tutorial.update(world, 1000).empty(), "map change clears tutorial overlays");
    world.initialize(20, 20, "Tutorial");
    check(tutorial.update(world, 1100).empty(), "missing player hides tutorial");
    std::cout << "Tutorial proximity, animation, raster and lifecycle checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
