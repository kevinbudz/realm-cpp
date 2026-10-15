#pragma once
#include "realm/messaging/Session.hpp"
#include "flash_assets.hpp"
#include <filesystem>
#include <string>

namespace realm::application {
struct Options {
  std::filesystem::path root{realm::flash::source_root};
  std::filesystem::path capture;
  std::filesystem::path map;
  std::filesystem::path account_file;
  std::filesystem::path settings_file;
  std::filesystem::path script;
  // The startup flow (loading, title, characters, gameplay) is the default; --connect, --map and
  // --stress clear it.
  bool client{true};
  std::uint16_t app_port{7777};
  // --audio auto|on|off|null: sound output. auto plays only in the interactive client, never in
  // fixtures or tools.
  std::string audio{"auto"};
  int width{800};
  int height{600};
  int frames{};
  // The HUD, menus and text UI are always on. world is set for every mode (internal; not a flag).
  bool hidden{};
  bool benchmark{};
  bool world{};
  bool connect{};
  bool password_stdin{};
  // Fixture inputs for the post effects (--map only): player condition bits and breath stat, plus a
  // fixed effect clock.
  std::uint32_t condition{};
  int breath{-1};
  std::int32_t effect_time{-1};
  // Status fixtures (--map only): health, a second player with a name, floating text and a
  // simulated hit.
  int hp{-1};
  int damage{};
  int status_age{};
  std::int32_t scene_time{-1};
  bool ghost{};
  bool ally{};
  std::string ally_name;
  std::string notify;
  std::string damage_effects;
  std::filesystem::path telemetry;
  std::filesystem::path trace;
  std::filesystem::path stress;
  std::filesystem::path stress_summary;
  int stress_delay{};
  int fixture_step{};
  int telemetry_warmup{30};
  realm::SessionOptions session;
};

Options parse(int argc, char** argv);
// Offline fixture initialization only; live state is owned by Session/World.
void load_offline_map(World& world, const Catalog& catalog, const Options& options);
}
