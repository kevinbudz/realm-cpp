#pragma once
#include "realm/renderer.hpp"
#include <memory>
#include <optional>
#include <string_view>

namespace realm {
// Registered Flash OBJ models (EmbeddedAssets.models_), keyed by their exact
// object XML <Model> name. Geometry is immutable and generated at build time.
[[nodiscard]] bool has_model(std::string_view name);
[[nodiscard]] std::size_t model_count();
[[nodiscard]] std::string_view model_name(std::size_t index);
[[nodiscard]] std::size_t model_triangle_count(std::string_view name);

// GameObject.setPosition(x, y, 0, Rotation): rotate about Z, then translate.
// Faces are culled with the Flash screen-space winding rule and shaded flat as
// 0.75 + 0.25 * max(0, n . L), with L = normalize(1, 3, 2) in world space.
// Returns null when the model is unknown or entirely back-facing.
[[nodiscard]] std::shared_ptr<const MeshDraw> project_model(
    std::string_view name, const Camera& camera, Vec2 position, double rotation_degrees,
    std::uint32_t color, const std::optional<SpriteRegion>& sprite);
}
