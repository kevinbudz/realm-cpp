#include "realm/ui/guild/GuildMembers.hpp"
#include "realm/messaging/Session.hpp"
#include "flash_assets.hpp"
#include <chrono>
#include <iostream>
#include <thread>

namespace {
int checks{};

void check(bool b, const char* label) {
  ++checks;
  if (!b) {
    throw std::runtime_error(label);
  }
}

using G = realm::GuildMembers;
using S = G::State;
using A = G::Action;

void click(G& g, realm::Vec2 p) {
  g.pointer_down(p);
  g.pointer_up(p);
}

void action(G& g, int row, int a) {
  click(g, {g.position().x + (a == 0   ? 686
                              : a == 1 ? 716
                                       : 750) *
                                 g.scale(),
            (126 + row * 32) * g.scale()});
}

void button(G& g, int b, const realm::FontBook& f) {
  g.update(f, 800, 600);
  const auto r = g.dialog().button(b);
  const auto p = g.dialog().position();
  click(g, {p.x + r.x + 5, p.y + r.y + 5});
  g.update(f, 800, 600);
}

void wait(G& g, const realm::FontBook& f) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(12);
  while (g.state() == S::loading && std::chrono::steady_clock::now() < end) {
    g.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  g.update(f, 800, 600);
  check(g.state() != S::loading, "HTTP completed");
}

realm::GuildMemberList fixture() {
  return {"Native Guild",
          12345,
          2,
          {{"Founder", 40, 100},
           {"Target", 0, 20},
           {"Leader", 30, 10},
           {"Officer", 20, 90},
           {"Member", 10, 55}}};
}
}

int main(int argc, char** argv) {
  try {
    realm::FontBook f;
    realm::HudArt art;
    G g(art);
    if (argc > 1 && std::string(argv[1]) == "http") {
      const std::string mode = argv[2];
      std::string password;
      std::getline(std::cin, password);
      const auto open = [&] {
        g.open("127.0.0.1", static_cast<std::uint16_t>(std::stoi(argv[3])),
               {{"username", argv[4]}, {"password", password}}, "Native Guild", "Founder",
               std::stoi(argv[5]));
      };
      open();
      check(g.state() == S::loading, "Async loading");
      if (mode == "stale") {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        auto t = std::chrono::steady_clock::now();
        g.close();
        check(std::chrono::steady_clock::now() - t < std::chrono::milliseconds(50),
              "Close nonblocking");
        open();
      }
      wait(g, f);
      if (mode == "error" || mode == "retry") {
        check(g.state() == S::error, "Load error dialog");
        if (mode == "retry") {
          button(g, 0, f);
          wait(g, f);
          check(g.state() == S::list, "Retry recovers");
        } else {
          check(!g.dialog().content().body.empty(), "Error body");
          button(g, 1, f);
          check(!g.visible(), "Error Close");
        }
      } else {
        check(g.state() == S::list, "Loaded members");
        check(g.list().name == "Native Guild", "Missing XML guild name uses current player");
        check(g.list().fame == 321, "CurrentFame");
        check(g.list().members.size() == 2, "Members count");
        check(g.list().members[0].name == "Zed café & +", "UTF8/XML/order preserved");
        check(g.list().members[1].rank == 20 && g.list().members[1].fame == 91, "Rank and fame");
      }
      std::cout << "Native guild member HTTP " << mode << " passed (" << checks << " checks)\n";
      return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "live") {
      std::string password;
      std::getline(std::cin, password);
      const int rank = std::stoi(argv[6]);
      realm::Catalog catalog(realm::flash::source_root);
      realm::World world(catalog);
      realm::Session session(world);
      realm::SessionOptions o;
      o.host = "127.0.0.1";
      o.port = static_cast<std::uint16_t>(std::stoi(argv[3]));
      o.username = argv[4];
      o.password = password;
      o.character_id = 0;
      session.connect(o);
      const auto start = std::chrono::steady_clock::now();
      int stage = 0;
      bool sent = false;
      while (stage < 4) {
        const auto time =
            static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start)
                                          .count());
        if (time > 15000) {
          throw std::runtime_error("Guild member live timeout stage " + std::to_string(stage));
        }
        session.poll(time);
        for (const auto& e : session.take_events()) {
          if (e.id == realm::flash::PacketId::TEXT) {
            g.receive(std::get<realm::net::incoming::Text>(e.data), world.player_id());
          }
        }
        if (session.state() == realm::SessionState::failed ||
            session.state() == realm::SessionState::disconnected) {
          throw std::runtime_error(session.error());
        }
        if (stage == 0 && session.state() == realm::SessionState::playing &&
            world.entity(world.player_id())) {
          g.open("127.0.0.1", static_cast<std::uint16_t>(std::stoi(argv[2])), session.credentials(),
                 "Native Guild", argv[4], 40);
          stage = 1;
        }
        g.poll(.002);
        g.update(f, 800, 600);
        if (auto request = g.take_request()) {
          if (request->action == A::remove) {
            session.leave_guild(request->name);
          } else {
            session.change_guild_rank(request->name, request->rank);
          }
          sent = true;
        }
        if (g.state() == S::list) {
          if (sent) {
            if (rank == 10) {
              throw std::runtime_error("Denied action succeeded");
            }
            const auto n = g.list().members.size();
            check(n == (stage == 3 ? 1U : 2U), "Live list after action");
            if (stage < 3) {
              check(g.list().members[1].rank == (stage == 1 ? 10 : 0), "Live rank refreshed");
            }
            ++stage;
            sent = false;
          }
          if (stage < 4) {
            if (stage == 1) {
              check(g.list().fame == 321 && g.list().level == 2, "Live guild fame/level");
              check(g.list().members[0].fame == 700, "Live actor fame");
              check(g.list().members[1].fame == 125, "Live target fame");
            }
            check(g.list().members[1].name == argv[5], "Stored membership order");
            action(g, 1, stage == 1 ? 0 : stage == 2 ? 1 : 2);
            check(g.state() == S::confirm, "Live confirmation");
            button(g, 1, f);
            check(g.state() == S::pending, "Live pending");
          }
        }
        if (g.state() == S::error) {
          check(rank == 10 && g.dialog().content().body ==
                                  (stage == 1 ? "No permission"
                                              : "Can't remove member. Insufficient privileges."),
                "Live server permission rejection shown through chat");
          button(g, 0, f);
          wait(g, f);
          check(g.list().members[1].rank == 0, "Denied action leaves persisted rank");
          sent = false;
          stage = stage == 1 ? 3 : 4;
        }
        session.finish_frame(time);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      session.disconnect();
      std::cout << "Native real guild HTTP/packet/chat "
                << (rank == 10 ? "permission rejection" : "promote/demote/remove") << " passed ("
                << checks << " checks)\n";
      return 0;
    }
    // Independent permission matrix: officer cannot create another officer;
    // leader may promote officer to peer leader; founder may never create founder.
    for (int actor : {0, 10, 20, 30, 40}) {
      for (int target : {0, 10, 20, 30, 40}) {
        check(G::allowed(actor, target, A::remove) == (actor >= 20 && target < actor),
              "Remove rank matrix");
        const bool promote = (actor == 20 && target == 0) ||
                             (actor == 30 && (target == 0 || target == 10 || target == 20)) ||
                             (actor == 40 && (target == 0 || target == 10 || target == 20));
        const bool demote = (actor == 20 && target == 10) ||
                            (actor == 30 && (target == 10 || target == 20)) ||
                            (actor == 40 && (target == 10 || target == 20 || target == 30));
        check(G::allowed(actor, target, A::promote) == promote, "Promote rank matrix");
        check(G::allowed(actor, target, A::demote) == demote, "Demote rank matrix");
      }
    }
    const auto xml = realm::parse_guild_members(
        "<Members><CurrentFame>42</CurrentFame><GuildLevel>3</GuildLevel><Member><Name>Zed &amp; "
        "café</Name><Rank>10</Rank><Fame>123</Fame></Member><Member><Name>Abe</Name><Rank>40</"
        "Rank></Member></"
        "Members>");
    check(xml.members[0].name == "Zed & café" && xml.members[1].name == "Abe",
          "XML stored order and escaped UTF8");
    check(xml.fame == 42 && xml.level == 3, "Guild fields");
    for (const char* value : {"<Error>No permission</Error>", "<Members>", "<Chars/>"}) {
      bool error = false;
      try {
        (void)realm::parse_guild_members(value);
      } catch (const std::exception&) {
        error = true;
      }
      check(error, "Malformed and Error XML rejected");
    }
    g.show(fixture(), "Founder", 40);
    check(g.update(f, 800, 600), "Initial rendering");
    check(!g.update(f, 800, 600), "Retained cache");
    action(g, 1, 0);
    check(g.state() == S::confirm, "Promote confirmation");
    check(g.dialog().content().body == "Are you sure you want to promote Target to Member?",
          "Exact source confirmation");
    button(g, 0, f);
    check(g.state() == S::list && !g.take_request(), "Cancel sends nothing");
    action(g, 1, 0);
    g.permissions("Native Guild", 10);
    check(g.state() == S::list, "Authoritative demotion cancels confirmation");
    action(g, 1, 0);
    check(g.state() == S::list, "Member actions absent");
    g.show(fixture(), "Founder", 40);
    g.update(f, 800, 600);
    action(g, 3, 1);
    button(g, 1, f);
    auto request = g.take_request();
    check(request && request->name == "Officer" && request->rank == 10,
          "Confirmed demotion emits rank packet request");
    check(!g.take_request(), "Request once");
    g.receive({"@Other", 99, 0, 0, "", "Officer has been demoted to Member."}, 99);
    check(g.state() == S::pending, "Other guild ignored");
    g.receive({"@Native Guild", 100, 0, 0, "", "Officer has been demoted to Member."}, 99);
    check(g.state() == S::pending, "Other actor ignored");
    g.receive({"*Error*", -1, 0, 0, "", "Combat error"}, 99);
    check(g.state() == S::pending, "Unrelated error ignored");
    g.receive({"*Error*", -1, 0, 0, "", "No permission"}, 99);
    check(g.state() == S::error && g.dialog().content().body == "No permission",
          "Rank chat error shown");
    g.show(fixture(), "Founder", 40);
    g.update(f, 800, 600);
    action(g, 1, 2);
    button(g, 1, f);
    request = g.take_request();
    check(request && request->action == A::remove && request->name == "Target",
          "Remove packet request");
    g.poll(10);
    check(g.state() == S::error && !g.take_request(), "Timeout does not resend mutation");
    g.show(fixture(), "Founder", 40);
    g.update(f, 800, 600);
    action(g, 1, 0);
    button(g, 1, f);
    g.permissions("Native Guild", 10);
    check(g.state() == S::list && !g.take_request(), "Demotion before send cancels request");
    auto list = fixture();
    for (int i = 0; i < 45; ++i) {
      list.members.push_back({"Row" + std::to_string(i), 0, i});
    }
    g.show(list, "Founder", 40);
    g.update(f, 800, 600);
    g.wheel(-10);
    g.update(f, 800, 600);
    check(g.scroll() > 0, "Wheel scrolls member list");
    g.wheel(1);
    g.update(f, 800, 600);
    g.pointer_down({786, 140});
    g.pointer_move({786, 510});
    g.pointer_up({786, 510});
    g.update(f, 800, 600);
    check(g.scroll() > .99, "Drag reaches bottom");
    g.focus_lost();
    check(!g.take_request(), "Scroll never emits action");
    g.show(fixture(), "Founder", 40);
    g.update(f, 800, 600);
    g.pointer_down({686, 158});
    f.set_pixel_scale(1.2);
    g.update(f, 1280, 720);
    g.pointer_up({640 + (686 - 400) * 1.2, 158 * 1.2});
    check(g.state() == S::list, "Resize cancels stale press");
    g.permissions("", 0);
    check(!g.visible(), "Leaving guild closes panel");
    std::cout << checks << " guild member checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
