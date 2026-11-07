#pragma once
// The uncompressed Targa loader of the RmlUi GPU backends (RmlGpuVk.cpp, RmlGpuWgpu.cpp): the decode of
// RenderInterface_GL3::LoadTexture, to premultiplied RGBA. Internal to realm_ui_rmlui.
#include <RmlUi/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

namespace realm::ui {
// The Targa header RmlUi's GL3 backend reads (18 bytes, packed).
#pragma pack(push, 1)
struct TgaHeader {
  char idLength;
  char colorMapType;
  char dataType;
  short colorMapOrigin;
  short colorMapLength;
  char colorMapDepth;
  short xOrigin;
  short yOrigin;
  short width;
  short height;
  char bitsPerPixel;
  char imageDescriptor;
};
#pragma pack(pop)

struct TargaImage {
  std::vector<Rml::byte> pixels;  // premultiplied RGBA
  Rml::Vector2i dimensions{};
};

// Decodes an uncompressed 24/32-bit Targa to premultiplied RGBA, as RenderInterface_GL3::LoadTexture.
inline std::optional<TargaImage> load_targa(const Rml::String& source) {
  auto* files = Rml::GetFileInterface();
  const auto file = files->Open(source);
  if (!file) {
    return std::nullopt;
  }
  files->Seek(file, 0, SEEK_END);
  const auto size = files->Tell(file);
  files->Seek(file, 0, SEEK_SET);
  if (size <= sizeof(TgaHeader)) {
    files->Close(file);
    Rml::Log::Message(Rml::Log::LT_ERROR, "Texture file '%s' is too small to be a TGA image.", source.c_str());
    return std::nullopt;
  }
  std::vector<Rml::byte> buffer(size);
  files->Read(buffer.data(), size, file);
  files->Close(file);

  TgaHeader header{};
  std::memcpy(&header, buffer.data(), sizeof header);
  const int color_mode = header.bitsPerPixel / 8;
  if (header.dataType != 2 || color_mode < 3 || header.width <= 0 || header.height <= 0) {
    Rml::Log::Message(Rml::Log::LT_ERROR, "Only 24/32bit uncompressed TGAs are supported ('%s').", source.c_str());
    return std::nullopt;
  }
  const auto width = static_cast<std::size_t>(header.width);
  const auto height = static_cast<std::size_t>(header.height);
  if (buffer.size() < sizeof header + width * height * static_cast<std::size_t>(color_mode)) {
    return std::nullopt;
  }
  const bool top_to_bottom = (header.imageDescriptor & 32) != 0;
  const Rml::byte* source_pixels = buffer.data() + sizeof header;
  TargaImage image;
  image.dimensions = {header.width, header.height};
  image.pixels.assign(width * height * 4, 0);
  for (std::size_t y = 0; y < height; ++y) {
    const std::size_t target_row = top_to_bottom ? y : height - 1 - y;
    for (std::size_t x = 0; x < width; ++x) {
      const Rml::byte* in = source_pixels + (y * width + x) * static_cast<std::size_t>(color_mode);
      Rml::byte* out = image.pixels.data() + (target_row * width + x) * 4;
      const Rml::byte alpha = color_mode == 4 ? in[3] : 255;
      // Targa stores BGR(A): swap to RGB and premultiply.
      out[0] = static_cast<Rml::byte>((in[2] * alpha) / 255);
      out[1] = static_cast<Rml::byte>((in[1] * alpha) / 255);
      out[2] = static_cast<Rml::byte>((in[0] * alpha) / 255);
      out[3] = alpha;
    }
  }
  return image;
}
}  // namespace realm::ui
