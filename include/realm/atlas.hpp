#pragma once
#include "realm/image.hpp"
#include <GL/glew.h>
#include <span>
#include <filesystem>
#include <vector>
#include <string_view>

namespace realm {
struct SpriteRegion { int page{}, x{}, y{}, width{}, height{}; };

class Atlas {
public:
    static constexpr int page_size = 2048;
    explicit Atlas(const std::filesystem::path& flash_root, std::span<const Image> additional = {}, std::size_t raw_prefix = 0);
    ~Atlas();
    Atlas(const Atlas&) = delete;
    Atlas& operator=(const Atlas&) = delete;
    [[nodiscard]] SpriteRegion sprite(std::size_t sheet, int index) const;
    [[nodiscard]] SpriteRegion image(std::size_t asset) const { return regions_.at(asset); }
    [[nodiscard]] SpriteRegion additional(std::size_t index) const { return regions_.at(asset_count_ + index); }
    [[nodiscard]] GLuint texture() const { return texture_; }
    void update_additional(std::size_t index, const Image& image);
    void update_additional_region(std::size_t index,const Image& image,int x,int y,int width,int height);
    [[nodiscard]] int pages() const { return pages_; }
    [[nodiscard]] std::size_t bytes() const { return static_cast<std::size_t>(pages_) * page_size * page_size * 4; }
private:
    std::vector<SpriteRegion> regions_;
    std::size_t asset_count_{};
    GLuint texture_{};
    int pages_{};
};
}
