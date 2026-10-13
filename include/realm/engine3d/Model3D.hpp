#pragma once
#include "realm/stage3D/DrawTypes.hpp"
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace realm {
// Registered Flash OBJ models (EmbeddedAssets.models_), keyed by their exact
// object XML <Model> name. Geometry is immutable and generated at build time.
[[nodiscard]] bool has_model(std::string_view name);
[[nodiscard]] std::size_t model_count();
[[nodiscard]] std::string_view model_name(std::size_t index);
// Index into model_name/model_range; empty for an unknown model.
[[nodiscard]] std::optional<std::uint32_t> model_index(std::string_view name);
[[nodiscard]] std::size_t model_triangle_count(std::string_view name);
// Conservative extent of a model about its origin, in tiles, for any rotation about Z: the farthest horizontal vertex
// distance and the highest/lowest vertex z. Zero for an unknown model.
struct ModelExtent {
  double radius{}, top{}, bottom{};
};
[[nodiscard]] ModelExtent model_extent(std::string_view name);

// Persistent geometry of every registered model, uploaded once by the renderer.
// Fan triangles keep their stored (i, 0, i+1) order, three vertices each, with the
// face normal (that of vertex i, as project_model shades) on all three; each
// model's material groups stay contiguous and in source order.
struct ModelVertex {
  float x, y, z, nx, ny, nz, u, v, solid;
};
struct ModelRange {
  std::uint32_t first{}, count{};
};
[[nodiscard]] std::span<const ModelVertex> model_geometry();
[[nodiscard]] ModelRange model_range(std::uint32_t index);

// GameObject.setPosition(x, y, 0, Rotation): rotate about Z, then translate.
// Faces are culled with the screen-space winding rule and shaded flat as
// 0.75 + 0.25 * max(0, n . L), with L = normalize(1, 3, 2) in world space.
// Returns null when the model is unknown or entirely back-facing.
// This is the CPU reference: the client draws model_quad instances, which the
// renderer projects, culls and shades on the GPU from model_geometry.
[[nodiscard]] std::shared_ptr<const MeshDraw> project_model(std::string_view name, const Camera& camera, Vec2 position,
                                                            double rotation_degrees, std::uint32_t color,
                                                            const std::optional<SpriteRegion>& sprite);

// The same model as one painter-ordered stream entry (kind 9 without an owner) and
// no allocation. The camera and object rotation are affine, so the quad carries the
// screen transform of model space: origin = model origin on screen, axis_x/axis_y/
// uv_offset = screen pixels per model x/y/z unit, offset = (cos, sin) of the object
// rotation and the model index, uv/page = sprite rectangle, multiply = flat color
// with alpha 1 when the sprite textures non-Solid groups.
[[nodiscard]] Quad model_quad(std::uint32_t index, const Camera& camera, Vec2 position, double rotation_degrees,
                              std::uint32_t color, const std::optional<SpriteRegion>& sprite);
}
