#include "realm/account.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace {
int checks{};
void check(bool ok, const char* why) {
  ++checks;
  if (!ok) throw std::runtime_error(why);
}
template <class F>
void rejects(F action) {
  bool rejected{};
  try {
    action();
  } catch (const std::exception&) {
    rejected = true;
  }
  check(rejected, "invalid Legends response rejected");
}
}
int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Expected Legends fixture");
    std::ifstream input(argv[1]);
    const std::string xml{std::istreambuf_iterator<char>(input), {}};
    const auto rows = realm::parse_legends(xml, "week");
    check(rows.size() == 20, "deduplicate by both IDs and ignore MyFameListElem");
    check(rows[0].account_id == 90 && rows[0].character_id == 100 && rows[2].account_id == 42 &&
              rows[2].character_id == 7,
          "explicit row identity");
    check(rows[0].character.skin == 836 && rows[0].character.tex1 == 16711680, "skin and dyes");
    check(rows[0].equipment == std::array<int, 4>{2594, 2595, -1, -1} && rows[0].item_data[0] == -1,
          "only equipped slots and data");
    auto unsorted = xml;
    unsorted.replace(unsorted.find("<TotalFame>5000"), 15, "<TotalFame>1");
    check(realm::parse_legends(unsorted, "week")[0].total_fame == 1,
          "server order is authoritative, never client sorted");
    check(realm::parse_legends("<FameList timespan='month'/>", "month").empty(), "empty valid list");
    const auto minimal =
        "<FameList timespan='all'><FameListElem accountId='1' "
        "charId='2'><ObjectType>782</ObjectType><TotalFame>0</TotalFame><Name>A &amp; "
        "B</Name></FameListElem><FameListElem accountId='1' "
        "charId='3'><ObjectType>782</ObjectType><TotalFame>5</TotalFame></FameListElem></FameList>";
    const auto optional = realm::parse_legends(minimal, "all");
    check(optional.size() == 2 && optional[0].name == "A & B" && optional[0].equipment[0] == -1 &&
              optional[0].character.skin == 0,
          "same-account different character and optional defaults");
    rejects([&] { (void)realm::parse_legends(xml, "month"); });
    for (const auto* bad : {"<FameList>", "<Chars/>", "<FameList/>", "<Error>Unavailable</Error>"})
      rejects([&] { (void)realm::parse_legends(bad, "week"); });
    for (const auto* bad : {"", "wat", "2147483648", "-5"}) {
      auto invalid = xml;
      const auto start = invalid.find("<TotalFame>") + 11;
      invalid.replace(start, 4, bad);
      rejects([&] { (void)realm::parse_legends(invalid, "week"); });
    }
    auto missing = xml;
    missing.replace(missing.find("accountId=\"90\""), 14, "");
    rejects([&] { (void)realm::parse_legends(missing, "week"); });
    std::string large = "<FameList timespan='week'>";
    for (int i = 0; i < 257; ++i)
      large +=
          "<FameListElem accountId='1' charId='2'><ObjectType>782</ObjectType><TotalFame>1</TotalFame></FameListElem>";
    large += "</FameList>";
    rejects([&] { (void)realm::parse_legends(large, "week"); });
    rejects([] { (void)realm::parse_legends(std::string(8 * 1024 * 1024 + 1, 'x'), "week"); });
    std::cout << checks << " Legends contract/order/identity/errors/bounds checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
