#include "realm/appengine/SavedCharactersList.hpp"
#include "realm/messaging/Session.hpp"
#include "flash_assets.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
int checks{};

void check(bool ok, const char* why) {
  ++checks;
  if (!ok) {
    throw std::runtime_error(why);
  }
}

template <class F>
void rejects(F action) {
  bool rejected{};
  try {
    action();
  } catch (const std::exception&) {
    rejected = true;
  }
  check(rejected, "invalid fame response rejected");
}
}

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error("Expected fame fixture path");
    }
    std::ifstream input(argv[1]);
    const std::string xml{std::istreambuf_iterator<char>(input), {}};
    const auto fame = realm::parse_character_fame(xml, 7);
    check(fame.name == "FameFixture" && fame.character.id == 7 && fame.character.skin == 836 &&
              fame.character.object_type == 782,
          "select requested identity and skin");
    check(
        fame.total_fame == 490 && fame.killed_by == "Medusa & Friends" && !fame.death_date.empty(),
        "authoritative total/killer/date");
    check(fame.lines.size() == 24 && fame.lines[0].label == "Minutes Active" &&
              fame.lines[0].value == 73,
          "stat count and first row");
    check(
        fame.lines[2].label == "Accuracy" && fame.lines[2].value == 33 && fame.lines[2].unit == "%",
        "truncated accuracy");
    check(fame.lines[17].label == "Dungeons Completed" && fame.lines[17].value == 28,
          "seven dungeon categories summed");
    check(fame.lines[20].label == "Party Member Level Ups" && fame.lines[21].value == 400 &&
              fame.lines[21].fame,
          "base follows all statistics");
    check(fame.lines[22].label == "Bonus: First Born" && fame.lines[22].value == 40 &&
              fame.lines[22].bonus && fame.lines[23].label == "Bonus: Enemy of the Gods",
          "server bonus ordering");
    auto zero = xml;
    zero.replace(zero.find("<Shots>300"), 10, "<Shots>0");
    check(realm::parse_character_fame(zero, 7).lines[2].value == 0, "zero-shot accuracy finite");
    const auto minimal = realm::parse_character_fame(
        "<Fame><Char "
        "id='-1'><ObjectType>782</ObjectType><Level>1</Level></Char><KilledOn>0</"
        "KilledOn><TotalFame>0</TotalFame></"
        "Fame>",
        -1);
    check(minimal.killed_by.empty() && minimal.lines.size() == 22 && minimal.lines[1].value == 0,
          "guest/no killer/missing optional statistics");
    rejects([&] { (void)realm::parse_character_fame(xml, 999); });
    rejects([] { (void)realm::parse_character_fame("<Error>Invalid character</Error>", 7); });
    rejects([] { (void)realm::parse_character_fame("<Fame>", 7); });
    rejects([] { (void)realm::parse_character_fame("<Chars/>", 7); });
    auto large = xml;
    for (int i = 0; i < 257; ++i) {
      large.insert(large.find("</Fame>"), "<Bonus id='test'>1</Bonus>");
    }
    rejects([&] { (void)realm::parse_character_fame(large, 7); });
    realm::Catalog catalog(realm::flash::source_root);
    realm::World world(catalog);
    world.initialize(8, 8);
    int sent{};
    realm::Session session(world, [&](const realm::net::Packet&) { ++sent; }, {});
    realm::net::Writer death;
    death.i32(42);
    death.i32(7);
    death.utf("Medusa");
    realm::net::Framer framer;
    const auto wire = realm::net::frame(realm::flash::PacketId::DEATH, death.bytes());
    check(framer.feed(std::span(wire).first(3)).empty(), "fragmented death length");
    for (const auto& packet : framer.feed(std::span(wire).subspan(3))) {
      session.replay(packet, 100);
    }
    check(session.state() == realm::SessionState::dead && session.death()->account_id == 42 &&
              session.death()->character_id == 7,
          "packet death state and payload retained");
    session.replay({static_cast<std::uint8_t>(realm::flash::PacketId::DEATH), death.bytes()}, 101);
    realm::net::Writer success;
    success.i32(99);
    success.i32(8);
    session.replay(
        {static_cast<std::uint8_t>(realm::flash::PacketId::CREATE_SUCCESS), success.bytes()}, 102);
    session.shoot(0, 102);
    session.player_text("test");
    session.finish_frame(102);
    session.poll(103);
    check(session.state() == realm::SessionState::dead && sent == 0 &&
              session.take_events().size() == 1,
          "terminal death ignores duplicate/later packets and gameplay output");
    // /char/delete: only an exact empty <Success/> is a deletion.
    realm::parse_delete_result("<Success/>");
    realm::parse_delete_result("<Success></Success>");
    realm::parse_delete_result(" \n<Success/>\n");
    ++checks;
    for (const auto* server_error :
         {"Invalid account.", "Account in use!", "Issue deleting character"}) {
      bool matched{};
      try {
        realm::parse_delete_result(std::string("<Error>") + server_error + "</Error>");
      } catch (const std::exception& e) {
        matched = std::string(e.what()) == server_error;
      }
      check(matched, "server <Error> text is thrown verbatim");
      check(realm::app_error(std::string("<Error>") + server_error + "</Error>") ==
                std::string(server_error),
            "app_error recognizes the delete error");
    }
    for (const auto* bad :
         {"", "wat", "<Success>junk</Success>", "<Success><x/></Success>", "<Success/><Success/>",
          "<Chars/>", "<Error>", "<Success", "<html>502</html>"}) {
      rejects([&] { realm::parse_delete_result(bad); });
    }
    check(!realm::app_error("<Success/>") && !realm::app_error("") && !realm::app_error("wat"),
          "success, empty and garbage are not explicit errors");
    std::cout << checks
              << " fame values/order/errors/bounds, delete replies and fragmented terminal death "
                 "checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
