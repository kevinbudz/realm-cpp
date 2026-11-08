#pragma once
#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Game asset access. The Flash art, XML, fonts and shapes are compiled into the executable
// (tools/embed_game_assets.py), so every asset reader goes through read_asset: a path under the
// virtual root "embedded:" is served from the executable, and any other path is read from disk
// (--assets-root names a directory with the same layout).
namespace realm::assets {
// The virtual root of the compiled-in assets. A path under it names a file by its path in the
// assets directory, for example "embedded:/images/lofiObj.png". flash::assets_root is this string.
inline constexpr std::string_view embedded_root = "embedded:";

// The embedded bytes of one asset, keyed by its path in the assets directory
// ("images/lofiObj.png"). Defined by the generated translation unit; nullopt when the key is not
// embedded.
std::optional<std::span<const unsigned char>> embedded_file(std::string_view key);

// The bytes of a game asset, or nullopt when it is missing, unreadable or larger than max_bytes.
[[nodiscard]] std::optional<std::vector<unsigned char>> read_asset(
    const std::filesystem::path& path,
    std::size_t max_bytes = std::numeric_limits<std::size_t>::max());

// True when read_asset would find the asset.
[[nodiscard]] bool asset_exists(const std::filesystem::path& path);
}  // namespace realm::assets
