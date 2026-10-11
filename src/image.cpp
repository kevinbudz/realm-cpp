#include "realm/image.hpp"
#include <png.h>
#include <stdexcept>
namespace realm {
void normalize_flash_png(Image& image) {
    for(std::size_t i=0;i<image.pixels.size();i+=4) {
        const unsigned alpha=image.pixels[i+3];if(alpha==255)continue;
        for(unsigned c=0;c<3;++c) image.pixels[i+c]=static_cast<unsigned char>(alpha ? ((static_cast<unsigned>(image.pixels[i+c])*alpha>>8U)<<8U)/alpha : 0);
    }
}
void premultiply_flash_bitmap(Image& image) {
    for(std::size_t i=0;i<image.pixels.size();i+=4)
        for(unsigned c=0;c<3;++c) if(image.pixels[i+3]!=255)
            image.pixels[i+c]=static_cast<unsigned char>((static_cast<unsigned>(image.pixels[i+c])*image.pixels[i+3]+255U)>>8U);
}
namespace {
Image finish_png(png_image& png) {
    png.format = PNG_FORMAT_RGBA;
    if (png.width > 8192 || png.height > 8192) {
        png_image_free(&png); throw std::runtime_error("PNG exceeds asset size limit");
    }
    Image result{static_cast<int>(png.width), static_cast<int>(png.height), std::vector<unsigned char>(PNG_IMAGE_SIZE(png))};
    if (!png_image_finish_read(&png, nullptr, result.pixels.data(), 0, nullptr)) {
        const std::string error = png.message; png_image_free(&png); throw std::runtime_error(error);
    }
    png_image_free(&png); return result;
}
}
Image load_png(const std::filesystem::path& path) {
    png_image png{};png.version=PNG_IMAGE_VERSION;
    if(!png_image_begin_read_from_file(&png,path.string().c_str()))throw std::runtime_error("Cannot read PNG "+path.string()+": "+png.message);
    return finish_png(png);
}
Image load_png(std::span<const unsigned char> bytes) {
    png_image png{};png.version=PNG_IMAGE_VERSION;
    if(!png_image_begin_read_from_memory(&png,bytes.data(),bytes.size()))throw std::runtime_error(std::string("Cannot read bundled PNG: ")+png.message);
    return finish_png(png);
}
void save_png(const std::filesystem::path& path, const Image& image) {
    if (image.width <= 0 || image.height <= 0 || image.pixels.size() !=
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4)
        throw std::invalid_argument("Invalid PNG image data");
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    png_image png{}; png.version = PNG_IMAGE_VERSION;
    png.width = static_cast<png_uint_32>(image.width); png.height = static_cast<png_uint_32>(image.height);
    png.format = PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&png, path.string().c_str(), 0, image.pixels.data(), 0, nullptr))
        throw std::runtime_error("Cannot write PNG " + path.string() + ": " + png.message);
}

}
