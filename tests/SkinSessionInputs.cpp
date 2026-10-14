#include "realm/ui/SkinChooser.hpp"
#include "realm/ui/menu/PlayerGroup.hpp"
#include "realm/messaging/Session.hpp"
#include "flash_assets.hpp"
#include <chrono>
#include <thread>
#include <iostream>
int main(int argc, char** argv) {
  try {
    if (argc != 4) throw std::runtime_error("Expected app/game/peer port");
    std::string password;
    std::getline(std::cin, password);
    using namespace realm;
    Catalog catalog(flash::source_root);
    World world(catalog), other(catalog);
    Session actor(world), peer(other);
    const auto data =
        parse_char_list(net::http_post("127.0.0.1", static_cast<std::uint16_t>(std::stoi(argv[1])), "/char/list",
                                       {{"username", "SkinActor"}, {"password", password}}));
    SessionOptions options;
    options.username = "SkinActor";
    options.password = password;
    options.port = static_cast<std::uint16_t>(std::stoi(argv[2]));
    options.create_character = true;
    options.skin_type = 836;
    actor.connect(options);
    options.username = "SkinPeer";
    options.port = static_cast<std::uint16_t>(std::stoi(argv[3]));
    options.skin_type = 0;
    peer.connect(options);
    FontBook fonts;
    HudArt art;
    SkinChooser chooser(catalog, fonts, art);
    PlayerGroup group(catalog, fonts, art);
    const auto start = std::chrono::steady_clock::now();
    int stage = 0;
    std::int32_t sent_at{};
    int actor_id = -1;
    std::vector<std::string> errors;
    auto click = [&](Vec2 p) {
      chooser.pointer_down(p);
      chooser.pointer_up(p);
    };
    while (stage != 11) {
      const auto time = static_cast<std::int32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
      if (time > 20000) throw std::runtime_error("Skin live timeout stage=" + std::to_string(stage));
      actor.poll(time);
      peer.poll(time);
      world.update(1);
      other.update(1);
      chooser.update(800, 600, time);
      for (const auto& e : actor.take_events())
        if (e.id == flash::PacketId::TEXT) {
          const auto& t = std::get<net::incoming::Text>(e.data);
          if (t.name == "*Error*") errors.push_back(t.text);
        }
      (void)peer.take_events();
      const auto* p = world.entity(world.player_id());
      const auto* remote = other.entity(actor_id);
      if (stage == 0 && p && peer.state() == SessionState::playing && time > 1500) {
        actor_id = p->id;
        if (p->appearance.texture != 836) throw std::runtime_error("CREATE selected skin not applied");
        chooser.endpoint("127.0.0.1", static_cast<std::uint16_t>(std::stoi(argv[1])), actor.credentials());
        chooser.open(782, p->appearance.texture, data);
        stage = 1;
      }
      if (stage == 1 && chooser.ready() && remote) {
        if (remote->appearance.texture != 836) throw std::runtime_error("Observer initial skin missing");
        const std::array<std::int32_t, 1> ids{actor_id};
        group.open(player_group_targets(other, ids), true, {100, 100}, 800, 600, 1);
        if (!chooser.owned(836) || chooser.owned(835) || chooser.owned(848))
          throw std::runtime_error("Fresh owned/class filter wrong");
        const auto lp = chooser.list_position();
        click({lp.x + 100, lp.y + 10});
        click({(800 - 462) / 2 + 300, (600 - chooser.view_height()) / 2 + chooser.view_height() - 25});
        auto skin = chooser.take_selection();
        if (skin != 0 || !actor.reskin(*skin, chooser.data().owned_skins))
          throw std::runtime_error("Native default reset selection failed");
        stage = 2;
      }
      if (stage == 2 && p && p->appearance.texture == 0 && remote && remote->appearance.texture == 0) {
        if (!group.refresh_appearance(other) || group.players()[0].skin != 0)
          throw std::runtime_error("Observer group default portrait stale");
        chooser.open(782, 0, data);
        stage = 3;
      }
      if (stage == 3 && chooser.ready()) {
        const auto lp = chooser.list_position();
        click({lp.x + 100, lp.y + 75});
        click({(800 - 462) / 2 + 300, (600 - chooser.view_height()) / 2 + chooser.view_height() - 25});
        auto skin = chooser.take_selection();
        if (skin != 836 || !actor.reskin(*skin, chooser.data().owned_skins))
          throw std::runtime_error("Native owned selection failed");
        stage = 4;
      }
      if (stage == 4 && p && p->appearance.texture == 836 && remote && remote->appearance.texture == 836) {
        if (!group.refresh_appearance(other)) throw std::runtime_error("Observer group reskin stale");
        if (actor.reskin(835, chooser.data().owned_skins) || actor.reskin(848, chooser.data().owned_skins) ||
            actor.reskin(65535, chooser.data().owned_skins))
          throw std::runtime_error("Invalid client request accepted");
        chooser.open(782, 836, data);
        chooser.update(800, 600, time);
        const auto lp = chooser.list_position();
        click({lp.x + 350, lp.y + 155});
        stage = 40;
      }
      if (stage == 40 && chooser.modal_input()) {
        auto receipt = chooser.take_purchase();
        if (!receipt || receipt->error != "Account in use!" || chooser.owned(848) || chooser.data().credits != 1000)
          throw std::runtime_error("Live purchase rejection changed ownership/currency");
        actor.send(net::outgoing::Reskin{848});
        stage = 5;
      }
      if (stage == 5 && !errors.empty()) {
        if (errors.back() != "Skin not owned." || p->appearance.texture != 836)
          throw std::runtime_error("Unowned rejection wrong");
        actor.send(net::outgoing::Reskin{835});
        stage = 6;
      }
      if (stage == 6 && errors.size() == 2) {
        if (errors.back() != "Skin is for different class." || p->appearance.texture != 836)
          throw std::runtime_error("Class rejection wrong");
        actor.send(net::outgoing::Reskin{65535});
        stage = 7;
      }
      if (stage == 7 && errors.size() == 3) {
        if (errors.back() != "Unknown skin type." || p->appearance.texture != 836)
          throw std::runtime_error("Unknown rejection wrong");
        actor.send(net::outgoing::Reskin{0});
        sent_at = time;
        stage = 8;
      }
      if (stage == 8 && p->appearance.texture == 0 && remote && remote->appearance.texture == 0) {
        if (!group.refresh_appearance(other)) throw std::runtime_error("Live final default stale");
        stage = 9;
      }
      if (stage == 9 && time > sent_at + 250) {
        if (remote->appearance.texture != 0) throw std::runtime_error("Observer final default wrong");
        stage = 11;
      }
      if (actor.state() == SessionState::failed || peer.state() == SessionState::failed)
        throw std::runtime_error(actor.error() + peer.error());
      actor.finish_frame(time);
      peer.finish_frame(time);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    actor.disconnect();
    peer.disconnect();
    std::cout << "Native fresh ownership/class chooser, CREATE skin, RESKIN owned/reset, 3 real rejections, online "
                 "HTTP purchase refusal and observer/live group portraits passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
