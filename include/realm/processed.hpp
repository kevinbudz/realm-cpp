#pragma once
#include "realm/animation.hpp"
#include <unordered_map>
namespace realm {
[[nodiscard]] std::string static_texture_key(int sheet, int index, int size, bool pad_bottom = false);
[[nodiscard]] std::string animated_texture_key(std::size_t frame, int size, bool pad_bottom = false);
class ProcessedTextures {
public:
    explicit ProcessedTextures(const std::filesystem::path& directory, std::size_t first_index);
    void append(const std::filesystem::path& directory,std::size_t first_index);
    [[nodiscard]] bool contains(const std::string& key) const { return lookup_.contains(key); }
    [[nodiscard]] std::size_t find(const std::string& key) const;
    [[nodiscard]] const std::vector<Image>& images() const { return images_; }
private:
    std::vector<Image> images_;
    std::unordered_map<std::string, std::size_t> lookup_;
};
}
