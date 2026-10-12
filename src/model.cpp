#include "realm/telemetry.hpp"
#include "realm/model.hpp"
#include "flash_models.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

namespace realm {
namespace {
const std::unordered_map<std::string_view, std::uint32_t>& names() {
    static const auto table = [] {
        std::unordered_map<std::string_view, std::uint32_t> result;
        for (std::uint32_t i = 0; i < flash::model_table.size(); ++i) result.emplace(flash::model_table[i].name, i);
        return result;
    }();
    return table;
}
const flash::ModelData* find(std::string_view name) {
    const auto found = names().find(name);
    return found == names().end() ? nullptr : &flash::model_table[found->second];
}
struct Geometry { std::vector<ModelVertex> vertices; std::vector<ModelRange> ranges; };
const Geometry& geometry() {
    static const auto built = [] {
        Geometry result;
        for (const auto& model : flash::model_table) {
            const auto first = static_cast<std::uint32_t>(result.vertices.size());
            for (std::uint32_t g = 0; g < model.group_count; ++g) {
                const auto& group = flash::model_groups[model.first_group + g];
                for (std::uint32_t t = group.first_index; t + 2 < group.first_index + group.index_count; t += 3) {
                    const auto& n = flash::model_vertices[model.first_vertex + flash::model_indices[t]];
                    for (std::uint32_t k = 0; k < 3; ++k) {
                        const auto& v = flash::model_vertices[model.first_vertex + flash::model_indices[t + k]];
                        result.vertices.push_back({v.x, v.y, v.z, n.nx, n.ny, n.nz, v.u, v.v, group.solid ? 1.0F : 0.0F});
                    }
                }
            }
            result.ranges.push_back({first, static_cast<std::uint32_t>(result.vertices.size()) - first});
        }
        return result;
    }();
    return built;
}
}
std::optional<std::uint32_t> model_index(std::string_view name) {
    const auto found = names().find(name);
    if (found == names().end()) return std::nullopt;
    return found->second;
}
std::span<const ModelVertex> model_geometry() { return geometry().vertices; }
ModelRange model_range(std::uint32_t index) { return index < geometry().ranges.size() ? geometry().ranges[index] : ModelRange{}; }
Quad model_quad(std::uint32_t index, const Camera& camera, Vec2 position, double rotation_degrees,
                std::uint32_t color, const std::optional<SpriteRegion>& sprite) {
    const double radians = rotation_degrees * std::numbers::pi / 180, c = std::cos(radians), s = std::sin(radians);
    // world_to_screen is affine: its unit steps along world x, y and z give the model-space screen axes.
    const auto origin = camera.world_to_screen(position);
    const auto step = [&](Vec2 p, double z) { const auto q = camera.world_to_screen(p, z); return Vec2{q.x - origin.x, q.y - origin.y}; };
    const auto ex = step({position.x + 1, position.y}, 0), ey = step({position.x, position.y + 1}, 0), ez = step(position, 1);
    const auto f = [](double v) { return static_cast<float>(v); };
    Quad q;
    q.kind = 9;
    q.origin = {f(origin.x), f(origin.y)};
    q.axis_x = {f(ex.x * c + ey.x * s), f(ex.y * c + ey.y * s)};
    q.axis_y = {f(ey.x * c - ex.x * s), f(ey.y * c - ex.y * s)};
    q.uv_offset = {f(ez.x), f(ez.y)};
    q.multiply = {static_cast<float>((color >> 16U) & 255U) / 255, static_cast<float>((color >> 8U) & 255U) / 255,
                  static_cast<float>(color & 255U) / 255, sprite ? 1.0F : 0.0F};
    if (sprite) {
        constexpr float inv = 1.0F / Atlas::page_size;
        q.uv = {static_cast<float>(sprite->x) * inv, static_cast<float>(sprite->y) * inv,
                static_cast<float>(sprite->width) * inv, static_cast<float>(sprite->height) * inv};
        q.page = static_cast<float>(sprite->page);
    }
    q.offset = {f(c), f(s), static_cast<float>(index), 0};
    return q;
}
bool has_model(std::string_view name) { return find(name) != nullptr; }
std::size_t model_count() { return flash::model_table.size(); }
std::string_view model_name(std::size_t index) { return index < flash::model_table.size() ? flash::model_table[index].name : std::string_view{}; }
ModelExtent model_extent(std::string_view name) {
    const auto* model = find(name); if (!model) return {};
    ModelExtent result{0, -1e300, 1e300};
    for (std::uint32_t i = 0; i < model->vertex_count; ++i) {
        const auto& v = flash::model_vertices[model->first_vertex + i];
        result.radius = std::max(result.radius, std::hypot(static_cast<double>(v.x), static_cast<double>(v.y)));
        result.top = std::max(result.top, static_cast<double>(v.z)); result.bottom = std::min(result.bottom, static_cast<double>(v.z));
    }
    if (!model->vertex_count) result = {};
    return result;
}
std::size_t model_triangle_count(std::string_view name) {
    const auto* model = find(name); if (!model) return 0;
    std::size_t result{};
    for (std::uint32_t g = 0; g < model->group_count; ++g) result += flash::model_groups[model->first_group + g].index_count / 3;
    return result;
}
std::shared_ptr<const MeshDraw> project_model(std::string_view name, const Camera& camera, Vec2 position, double rotation_degrees,
                                              std::uint32_t color, const std::optional<SpriteRegion>& sprite) {
    REALM_ZONE("model.prepare");
    const auto* model = find(name); if (!model) return nullptr;
    const double radians = rotation_degrees * std::numbers::pi / 180, c = std::cos(radians), s = std::sin(radians);
    // Lighting3D.LIGHT_VECTOR.
    constexpr double light_length = 3.7416573867739413; // sqrt(14)
    constexpr double lx = 1 / light_length, ly = 3 / light_length, lz = 2 / light_length;
    std::vector<Vec2> screen(model->vertex_count);
    for (std::uint32_t i = 0; i < model->vertex_count; ++i) {
        const auto& v = flash::model_vertices[model->first_vertex + i];
        const double x = static_cast<double>(v.x), y = static_cast<double>(v.y);
        screen[i] = camera.world_to_screen({position.x + x * c - y * s, position.y + x * s + y * c}, static_cast<double>(v.z));
    }
    auto draw = std::make_shared<MeshDraw>();
    if (sprite) {
        constexpr float inv = 1.0F / Atlas::page_size;
        draw->bounds = {static_cast<float>(sprite->x) * inv, static_cast<float>(sprite->y) * inv,
                        static_cast<float>(sprite->width) * inv, static_cast<float>(sprite->height) * inv};
        draw->page = static_cast<float>(sprite->page);
    }
    draw->color = {static_cast<float>((color >> 16U) & 255U) / 255, static_cast<float>((color >> 8U) & 255U) / 255, static_cast<float>(color & 255U) / 255};
    for (std::uint32_t g = 0; g < model->group_count; ++g) {
        const auto& group = flash::model_groups[model->first_group + g];
        const auto first = static_cast<std::uint32_t>(draw->vertices.size());
        for (std::uint32_t t = group.first_index; t + 2 < group.first_index + group.index_count; t += 3) {
            // Triangles are stored as (i, 0, i+1) of a fan. Flash culls the fan
            // triangle (0, i, i+1) whose screen cross product is negative.
            const std::uint32_t index[3] = {flash::model_indices[t], flash::model_indices[t + 1], flash::model_indices[t + 2]};
            const auto& a = screen[index[0]], &b = screen[index[1]], &d = screen[index[2]];
            // Edge-on faces project to zero area; keep rounding noise from deciding their fate.
            if ((a.x - b.x) * (d.y - b.y) - (a.y - b.y) * (d.x - b.x) <= 1e-7) continue;
            const auto& n = flash::model_vertices[model->first_vertex + index[0]];
            const double nx = static_cast<double>(n.nx) * c - static_cast<double>(n.ny) * s;
            const double ny = static_cast<double>(n.nx) * s + static_cast<double>(n.ny) * c;
            const double shade = .75 + .25 * std::max(0.0, nx * lx + ny * ly + static_cast<double>(n.nz) * lz);
            for (const auto i : index) {
                const auto& v = flash::model_vertices[model->first_vertex + i];
                draw->vertices.push_back({static_cast<float>(screen[i].x), static_cast<float>(screen[i].y), v.u, v.v, static_cast<float>(shade)});
            }
        }
        const auto count = static_cast<std::uint32_t>(draw->vertices.size()) - first;
        if (count) draw->groups.push_back({group.solid != 0 || !sprite, first, count});
    }
    if (draw->groups.empty()) return nullptr;
    return draw;
}
}
