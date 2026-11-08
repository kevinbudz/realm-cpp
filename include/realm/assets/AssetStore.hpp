#pragma once
#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Game asset access. The Flash art, XML data and SFX are compiled into the executable (see
// tools/embed_game_assets.py), so every asset reader goes through read_asset: a path under a
// virtual root is served from the executable, and any other path is read from disk (a --flash-root
// or REALM_SFX_ROOT override).
namespace realm::assets {
// Virtual roots. flash::source_root and the default SFX root name these; they are never opened on
// disk.
inline constexpr std::string_view embedded_flash_root = "embedded:/flash";
inline constexpr std::string_view embedded_sfx_root = "embedded:/sfx";

// The embedded bytes of one asset, keyed "flash/<path under the Flash root>" or "sfx/<path under
// the SFX root>". Defined by the generated translation unit; nullopt when the key is not embedded.
std::optional<std::span<const unsigned char>> embedded_file(std::string_view key);

// The bytes of a game asset, or nullopt when it is missing, unreadable or larger than max_bytes.
[[nodiscard]] std::optional<std::vector<unsigned char>> read_asset(
    const std::filesystem::path& path,
    std::size_t max_bytes = std::numeric_limits<std::size_t>::max());

// True when read_asset would find the asset.
[[nodiscard]] bool asset_exists(const std::filesystem::path& path);
}  // namespace realm::assets
