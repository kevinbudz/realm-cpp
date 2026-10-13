#include "realm/util/Appearances.hpp"
#include "realm/ui/menu/PlayerGroup.hpp"
#include "realm/ui/tooltip/PlayerTooltip.hpp"
#include "realm/messaging/Session.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <thread>
#include <iostream>
int main(int argc, char** argv) {
  try {
    if (argc != 5) throw std::runtime_error("Expected game/peer port, cases and output");
    std::string password;
    std::getline(std::cin, password);
    using namespace realm;
    Catalog catalog(flash::source_root);
    World world(catalog), other(catalog);
    Session actor(world), peer(other);
    SessionOptions options;
    options.username = "DyeActor";
    options.password = password;
    options.port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    options.character_id = 0;
    actor.connect(options);
    options.username = "DyePeer";
    options.port = static_cast<std::uint16_t>(std::stoi(argv[2]));
    peer.connect(options);
    FontBook fonts;
    AnimationLibrary animations(flash::source_root);
    Appearances appearances(flash::source_root, animations);
    HudArt art;
    art.appearances(&appearances, &catalog);
    PlayerGroup group(catalog, fonts, art);
    PlayerTooltip tooltip(catalog, fonts, art);
    PlayerMenu menu(catalog, fonts, art);
    Hud hud(fonts, &art, &catalog);
    const auto cases = nlohmann::json::parse(std::ifstream(argv[3]));
    const std::filesystem::path output = argv[4];
    std::filesystem::create_directories(output);
    const auto start = std::chrono::steady_clock::now();
    int stage = -1;
    bool pending = false;
    std::int32_t sent{};
    int actor_id = -1;
    std::uint32_t tex1{}, tex2{};
    std::vector<unsigned char> previous;
    while (stage < static_cast<int>(cases.size()) + 1) {
      const auto time = static_cast<std::int32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
      if (time > 20000) throw std::runtime_error("Dye live timeout stage=" + std::to_string(stage));
      actor.poll(time);
      peer.poll(time);
      world.update(1);
      other.update(1);
      (void)actor.take_events();
      (void)peer.take_events();
      const auto* p = world.entity(world.player_id());
      if (stage == -1 && p && peer.state() == SessionState::playing && time > 1500) actor_id = p->id;
      const auto* remote = other.entity(actor_id);
      if (stage == -1 && p && remote) {
        if (p->appearance.size != 137 || p->appearance.tex1 != 0x1f0f8ff ||
            remote->appearance.tex1 != p->appearance.tex1)
          throw std::runtime_error("Initial stored live appearance missing");
        const std::array<std::int32_t, 1> ids{actor_id};
        group.open(player_group_targets(other, ids), true, {100, 100}, 800, 600, 1);
        if (const auto target = player_menu_target(other, peer.party(), actor_id, true))
          menu.open(*target, {100, 100}, 800, 600, 1);
        else
          throw std::runtime_error("Menu target missing");
        previous = group.image().pixels;
        tex1 = p->appearance.tex1;
        tex2 = p->appearance.tex2;
        stage = 0;
      }
      if (stage >= 0 && stage < static_cast<int>(cases.size()) && p && remote) {
        const auto& c = cases[static_cast<std::size_t>(stage)];
        if (!pending) {
          actor.use_item({actor_id, static_cast<std::uint8_t>(4 + stage)}, time);
          if (p->appearance.tex1 != tex1 || p->appearance.tex2 != tex2)
            throw std::runtime_error("Optimistic dye mutation");
          pending = true;
          sent = time;
        }
        const auto expected1 = c.at("tex1").get<std::uint32_t>(), expected2 = c.at("tex2").get<std::uint32_t>();
        if (time > sent + 100 && p->appearance.tex1 == expected1 && p->appearance.tex2 == expected2 &&
            remote->appearance.tex1 == expected1 && remote->appearance.tex2 == expected2) {
          if (!group.refresh_appearance(other) || group.image().pixels == previous)
            throw std::runtime_error("Observer live group portrait stale");
          previous = group.image().pixels;
          if (!menu.update(other, {100, 100}, false, 0, 1) || !tooltip.update(player_tooltip_target(other, actor_id)))
            throw std::runtime_error("Live menu/tooltip did not refresh");
          if (!hud.update(HudState::player(world, *p, &actor.party(), time)))
            throw std::runtime_error("Live HUD did not refresh");
          const auto appearance =
              appearances.portrait(catalog, p->type, p->appearance.texture, p->appearance.tex1, p->appearance.tex2);
          if (appearance->pixels.empty()) throw std::runtime_error("Missing live appearance");
          save_png(output / ("group-" + std::to_string(stage) + ".png"), group.image());
          save_png(output / ("hud-" + std::to_string(stage) + ".png"), hud.image());
          tex1 = p->appearance.tex1;
          tex2 = p->appearance.tex2;
          ++stage;
          pending = false;
        }
      }
      if (stage == static_cast<int>(cases.size()) && p && remote) {
        if (!pending) {
          if (!actor.reskin(836, std::array<int, 1>{836})) throw std::runtime_error("Dye skin request rejected");
          pending = true;
        }
        if (p->appearance.texture == 836 && remote->appearance.texture == 836) {
          if (p->appearance.tex1 != tex1 || p->appearance.tex2 != tex2 || !group.refresh_appearance(other))
            throw std::runtime_error("Dyes lost across skin");
          ++stage;
        }
      }
      if (actor.state() == SessionState::failed || peer.state() == SessionState::failed)
        throw std::runtime_error(actor.error() + peer.error());
      actor.finish_frame(time);
      peer.finish_frame(time);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    actor.disconnect();
    peer.disconnect();
    std::cout << "Native USEITEM dyes, all four textile sizes, stored size137, no optimistic dye, observer "
                 "HUD/group/menu/tooltip updates and retained dyes across RESKIN passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
