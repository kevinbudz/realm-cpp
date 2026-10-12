#pragma once
#include "realm/session.hpp"
#include "flash_assets.hpp"
#include <filesystem>
#include <string>

namespace realm::application {
struct Options {
    std::filesystem::path root{realm::flash::source_root}, capture, map, processed, texture, fonts, hud_art, account_file, settings_file, script;
    bool client{};
    std::uint16_t app_port{7777};
    std::string text;int font_size{14};bool font_bold{},font_shadow{},font_yhalf{};
    std::string sheet{"lofiEnvironment"};
    int width{800}, height{600}, frames{}, sprites{}, scale{4};
    bool hidden{}, benchmark{}, world{}, offset{}, connect{}, password_stdin{}, projectile_fixture{}, projectile_outline{};
    double projectile_angle{}; int projectile_size{100}; bool raw_flash_bitmap{};
    // Fixture inputs for the post effects (--map only): player condition bits, breath stat, and a fixed effect clock.
    std::uint32_t condition{}; int breath{-1}, background{}; std::int32_t effect_time{-1};
    // Status fixtures (--map only): health, a second player with a name, floating text and a simulated hit.
    int hp{-1}, damage{}, status_age{}; std::int32_t scene_time{-1}; bool ghost{}; bool ally{}; std::string ally_name, notify, damage_effects;
    std::filesystem::path telemetry, trace, stress, stress_summary;
    int stress_delay{}, fixture_step{};
    int telemetry_warmup{30};
    realm::SessionOptions session;
};
Options parse(int argc, char** argv, bool client_default);
int environment_integer(const char* name, int fallback, int low, int high);
int dynamic_atlas_maximum();
int dynamic_atlas_initial(int maximum);
// Offline fixture initialization only; live state is owned by Session/World.
void load_offline_map(World& world, const Catalog& catalog, const Options& options);
}
