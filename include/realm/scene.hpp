#pragma once
#include "realm/animation.hpp"
#include "realm/processed.hpp"
#include "realm/terrain.hpp"
#include "realm/renderer.hpp"
#include "realm/world.hpp"
namespace realm {
class Scene {
public:
    void build(const World& world, const Camera& camera, const Atlas& atlas, const AnimationLibrary& animations, const ProcessedTextures* processed, const Terrain* terrain,
               Vec2 camera_position, double camera_angle, double zoom, int width, int height, std::int32_t time_ms, std::vector<Quad>& output);
private:
    std::vector<const Entity*> visible_;
};
}
