#include "realm/map/partyoverlay/PartyOverlay.hpp"
#include "realm/ui/menu/PlayerGroup.hpp"
#include "realm/messaging/GameServerConnection.hpp"
#include "realm/objects/Player.hpp"
#include "flash_assets.hpp"
#include <chrono>
#include <thread>
#include <queue>
#include <iostream>
int main(int argc, char** argv) {
  try {
    if (argc != 3) throw std::runtime_error("Expected actor port and peer port");
    using namespace realm;
    Catalog catalog(flash::source_root);
    World actor_world(catalog), peer_world(catalog);
    Session actor(actor_world), peer(peer_world);
    std::string password;
    std::getline(std::cin, password);
    SessionOptions options;
    options.username = "ArrowActor";
    options.password = password;
    options.port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    options.character_id = 0;
    actor.connect(options);
    options.username = "ArrowPeer";
    options.port = static_cast<std::uint16_t>(std::stoi(argv[2]));
    peer.connect(options);
    FontBook fonts;
    HudArt art;
    PlayerGroup group(catalog, fonts, art);
    PartyArrows arrows;
    Camera camera;
    const auto start = std::chrono::steady_clock::now();
    auto previous = start;
    Vec2 spawn{};
    std::vector<Vec2> path;
    std::size_t step{};
    int first = 0, gotos = 0, stage = 0, target = -1;
    bool traveled = false;
    while (stage < 3) {
      const auto now = std::chrono::steady_clock::now();
      const auto time =
          static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
      const double dt = std::chrono::duration<double, std::milli>(now - previous).count();
      previous = now;
      if (time > 35000) throw std::runtime_error("Party live timeout, stage " + std::to_string(stage));
      actor.poll(time);
      peer.poll(time);
      actor_world.update(dt);
      peer_world.update(dt);
      for (const auto& e : actor.take_events())
        if (e.id == flash::PacketId::GOTO && std::get<net::incoming::Goto>(e.data).object_id == actor_world.player_id())
          ++gotos;
      (void)peer.take_events();
      auto* friend_player = peer_world.entity(peer_world.player_id());
      const auto* local = actor_world.entity(actor_world.player_id());
      if (time > 11000 && friend_player && local) {
        if (path.empty() && !traveled) {
          spawn = friend_player->motion.position;
          const int w = peer_world.width(), h = peer_world.height();
          std::vector<int> parents(static_cast<std::size_t>(w * h), -2);
          std::queue<int> todo;
          const int begin = static_cast<int>(spawn.x) + static_cast<int>(spawn.y) * w;
          parents[begin] = -1;
          todo.push(begin);
          int end = -1;
          while (!todo.empty()) {
            const int at = todo.front();
            todo.pop();
            const Vec2 p{at % w + .5, at / w + .5};
            const double dist = std::hypot(p.x - spawn.x, p.y - spawn.y);
            if (dist > 13 && dist < 14) {
              end = at;
              break;
            }
            if (dist > 14) continue;
            for (const auto delta : {Vec2{1, 0}, Vec2{0, 1}, Vec2{-1, 0}, Vec2{0, -1}}) {
              const int x = at % w + static_cast<int>(delta.x), y = at / w + static_cast<int>(delta.y);
              if (x < 0 || y < 0 || x >= w || y >= h) continue;
              const int next = x + y * w;
              if (parents[next] != -2 || !valid_player_position(peer_world, *friend_player, {x + .5, y + .5})) continue;
              Entity probe = *friend_player;
              probe.motion.position = p;
              const auto reached = modify_player_move(peer_world, probe, {x + .5, y + .5});
              if (std::hypot(reached.x - x - .5, reached.y - y - .5) > .001) continue;
              parents[next] = at;
              todo.push(next);
            }
          }
          if (end < 0) throw std::runtime_error("No walkable live peer route beyond source draw radius");
          for (int at = end; at != begin; at = parents[at]) path.push_back({at % w + .5, at / w + .5});
          std::reverse(path.begin(), path.end());
        }
        if (step < path.size()) {
          const auto to = path[step];
          const double dx = to.x - friend_player->motion.position.x, dy = to.y - friend_player->motion.position.y,
                       length = std::hypot(dx, dy);
          if (length < .06) {
            peer_world.goto_entity(friend_player->id, to);
            ++step;
          } else {
            double angle{};
            move_player(peer_world, *friend_player, {dx / length, dy / length}, 0, std::min(dt, 10.0), angle);
          }
        } else
          traveled = true;
        camera.configure(local->motion.position, 0, 800, 600);
        arrows.update(actor_world, actor.party(), camera, {}, false, 800, 600);
        if (stage == 0)
          for (const auto& arrow : arrows.arrows())
            if (!arrow.players.empty()) {
              const auto rows = player_group_targets(actor_world, arrow.players);
              const auto selected = first_teleport_player(actor_world, rows);
              if (!selected) continue;
              const auto* seen = actor_world.entity(*selected);
              if (!seen || seen->profile.name != "ArrowPeer") throw std::runtime_error("Wrong party member selected");
              if (!group.open(rows, actor.map_info().allow_teleport, arrow.position, 800, 600, 1))
                throw std::runtime_error("Arrow menu did not open");
              target = *selected;
              if (actor.teleport(target, time)) throw std::runtime_error("Live arrow teleport refused");
              first = time;
              if (!actor.teleport(target, time) || actor.teleport_cooldown(time) == 0)
                throw std::runtime_error("Cooldown missing");
              stage = 1;
              break;
            }
        if (stage == 1 && gotos == 1 && time - first >= 10000) {
          const auto selected = first_teleport_player(actor_world, group.players());
          if (selected != target || actor.teleport(*selected, time))
            throw std::runtime_error("Live snapshot expiry teleport failed");
          stage = 2;
        }
        if (stage == 2 && gotos == 2) stage = 3;
      }
      if (actor.state() == SessionState::failed || peer.state() == SessionState::failed)
        throw std::runtime_error(actor.error() + peer.error());
      actor.finish_frame(time);
      peer.finish_frame(time);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    actor.poll(first + 10020);
    peer.poll(first + 10020);
    actor.disconnect();
    peer.disconnect();
    std::cout << "Live peer movement exposed a source-undrawn party arrow; ordered arrow menu snapshot, cooldown "
                 "refusal/expiry and GOTO="
              << gotos << " passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
