#include "realm/backdrop.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace realm {
void Backdrop::reset(int kind, std::uint32_t seed) {
    kind_ = kind == 1 ? BackdropKind::stars : kind == 2 ? BackdropKind::nexus : BackdropKind::none;
    seed_ = seed; stars_.clear(); water_.reset(); water_image_.reset();
    if (kind_ == BackdropKind::stars) stars_ = star_field(seed);
}
void Backdrop::set_water(Image image) { water_image_ = std::make_unique<Image>(std::move(image)); water_.reset(); }
void Backdrop::build(const Camera& camera, const Atlas& atlas, std::int32_t time, std::vector<Quad>& out) {
    const auto origin = camera.origin();
    const double zoom = camera.scale(), angle = camera.angle();
    if (kind_ == BackdropKind::stars) {
        // Positions turn by -angle about the camera point; each star stays an upright bitmap.
        const double c = std::cos(-angle), s = std::sin(-angle);
        const auto sheet = stars_sheet_index();
        for (const auto& star : stars_) {
            const auto region = atlas.sprite(sheet, star.image);
            const double w = region.width * star.scale * zoom, h = region.height * star.scale * zoom;
            const Vec2 center{origin.x + (star.x * c - star.y * s) * zoom, origin.y + (star.x * s + star.y * c) * zoom};
            out.push_back(sprite_quad(region, {center.x - w / 2, center.y - h / 2}, {w, 0}, {0, h}));
        }
    } else if (kind_ == BackdropKind::nexus) {
        if (!water_) { water_ = std::make_unique<SurfaceTexture>(); water_->update(water_image_ ? *water_image_ : nexus_water(seed_)); }
        // Bitmap point b lands at R(-angle) * (b + drift * time) in map space; tile the bitmap over the clip.
        const double drift = water_drift * time, c = std::cos(-angle), s = std::sin(-angle);
        const auto place = [&](double bx, double by) { const double px = (bx + drift) * c - (by + drift) * s, py = (bx + drift) * s + (by + drift) * c; return Vec2{origin.x + px * zoom, origin.y + py * zoom}; };
        const auto clip = camera.clip_rect();
        double lo_x = 1e300, lo_y = 1e300, hi_x = -1e300, hi_y = -1e300;
        for (const auto& corner : std::array<Vec2, 4>{{{clip.x, clip.y}, {clip.x + clip.width, clip.y}, {clip.x, clip.y + clip.height}, {clip.x + clip.width, clip.y + clip.height}}}) {
            // Inverse of the placement: b = R(angle) * p - drift.
            const double bx = corner.x * c + corner.y * s - drift, by = -corner.x * s + corner.y * c - drift;
            lo_x = std::min(lo_x, bx); hi_x = std::max(hi_x, bx); lo_y = std::min(lo_y, by); hi_y = std::max(hi_y, by);
        }
        for (int ty = static_cast<int>(std::floor(lo_y / water_size)); ty <= static_cast<int>(std::floor(hi_y / water_size)); ++ty)
            for (int tx = static_cast<int>(std::floor(lo_x / water_size)); tx <= static_cast<int>(std::floor(hi_x / water_size)); ++tx) {
                const auto p = place(static_cast<double>(tx) * water_size, static_cast<double>(ty) * water_size);
                const auto px = place(static_cast<double>(tx + 1) * water_size, static_cast<double>(ty) * water_size);
                const auto py = place(static_cast<double>(tx) * water_size, static_cast<double>(ty + 1) * water_size);
                auto quad = water_->quad(p, {px.x - p.x, px.y - p.y}, {py.x - p.x, py.y - p.y});
                quad.kind = 3; // repeat shader path: nearest texels, opaque
                out.push_back(quad);
            }
    }
    (void)atlas;
}
}
