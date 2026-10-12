#pragma once
#include "realm/image.hpp"
#include "realm/draw_types.hpp"
#include <span>
#include <filesystem>
#include <vector>
#include <string_view>

namespace realm {

class Atlas {
public:
    static constexpr int page_size = sprite_page_size;
    // `dynamic_pages` extra zero-filled layers follow the static pages (same layer indices); Scene packs composed bitmaps into them.
    explicit Atlas(const std::filesystem::path& flash_root, std::span<const Image> additional = {}, std::size_t raw_prefix = 0, int dynamic_pages = 0);
    ~Atlas();
    Atlas(const Atlas&) = delete;
    Atlas& operator=(const Atlas&) = delete;
    [[nodiscard]] SpriteRegion sprite(std::size_t sheet, int index) const;
    [[nodiscard]] SpriteRegion image(std::size_t asset) const { return regions_.at(asset); }
    [[nodiscard]] SpriteRegion additional(std::size_t index) const { return regions_.at(asset_count_ + index); }
    [[nodiscard]] std::uint32_t texture() const { return texture_; }
    void update_additional(std::size_t index, const Image& image);
    void update_additional_region(std::size_t index,const Image& image,int x,int y,int width,int height);
    // Premultiplies a copy of `image` exactly as SurfaceTexture does and writes it at (x, y) of dynamic page `page`
    // (0 <= page < dynamic_pages()); the layer in SpriteRegion terms is dynamic_first() + page.
    void upload_dynamic(int page,int x,int y,const Image& image);
    // Reallocates the array with `pages` dynamic layers: every static layer and the first min(old, new) dynamic layers are copied
    // (glCopyImageSubData, or per-layer framebuffer blits without GL 4.3 / ARB_copy_image), the rest start zeroed. Layer indices and page
    // size do not change, so quads already built against the old texture stay valid. Replaces `texture()`.
    void resize_dynamic(int pages);
    [[nodiscard]] static bool hardware_copy();
    // Tests: exercise the framebuffer-blit resize path on drivers that also offer glCopyImageSubData.
    static inline bool force_blit_copy = false;
    [[nodiscard]] std::uint64_t resizes() const { return resizes_; }
    [[nodiscard]] int pages() const { return pages_ + dynamic_pages_; }
    [[nodiscard]] int dynamic_first() const { return pages_; }
    [[nodiscard]] int dynamic_pages() const { return dynamic_pages_; }
    [[nodiscard]] std::size_t bytes() const { return static_cast<std::size_t>(pages()) * page_size * page_size * 4; }
    [[nodiscard]] std::size_t dynamic_bytes() const { return static_cast<std::size_t>(dynamic_pages_) * page_size * page_size * 4; }
private:
    std::vector<SpriteRegion> regions_;
    std::size_t asset_count_{};
    std::uint32_t texture_{};
    int pages_{}, dynamic_pages_{};
    std::uint64_t resizes_{};
};
}
