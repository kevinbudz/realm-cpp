#include "realm/assets/AssetStore.hpp"

#include <fstream>
#include <ios>
#include <string>
#include <system_error>

namespace realm::assets {
namespace {
constexpr std::string_view virtual_prefix = "embedded:/";

// The embedded key of a path under a virtual root, or nullopt for a disk path. A path that merely
// names a missing embedded file yields a key that is not in the table, so it is never read from
// disk.
std::optional<std::string> embedded_key(const std::filesystem::path& path) {
  const auto text = path.generic_string();
  if (text.compare(0, virtual_prefix.size(), virtual_prefix) != 0) {
    return std::nullopt;
  }
  return text.substr(virtual_prefix.size());
}
}  // namespace

std::optional<std::vector<unsigned char>> read_asset(const std::filesystem::path& path,
                                                     std::size_t max_bytes) {
  if (const auto key = embedded_key(path)) {
    const auto bytes = embedded_file(*key);
    if (!bytes || bytes->size() > max_bytes) {
      return std::nullopt;
    }
    return std::vector<unsigned char>(bytes->begin(), bytes->end());
  }
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    return std::nullopt;
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > max_bytes) {
    return std::nullopt;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return std::nullopt;
  }
  std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
  if (!file.read(reinterpret_cast<char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()))) {
    return std::nullopt;
  }
  return bytes;
}

bool asset_exists(const std::filesystem::path& path) {
  if (const auto key = embedded_key(path)) {
    return embedded_file(*key).has_value();
  }
  std::error_code error;
  return std::filesystem::exists(path, error);
}
}  // namespace realm::assets
