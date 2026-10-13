#include "realm/classes/CharacterSkinListView.hpp"
#include "flash_assets.hpp"
#include <chrono>
#include <thread>
#include <iostream>
int main(int argc, char** argv) {
  try {
    if (argc != 3 && argc != 4) throw std::runtime_error("Expected mode/app port [host]");
    using namespace realm;
    FontBook fonts;
    HudArt art;
    Catalog catalog(flash::source_root);
    SkinChooser chooser(catalog, fonts, art);
    const std::string mode = argv[1];
    const auto port = static_cast<std::uint16_t>(std::stoi(argv[2]));
    chooser.fetch(782, 0, argc == 4 ? argv[3] : "localhost", port,
                  {{"username", "Fixture"}, {"password", "private-fixture"}});
    auto pump = [&](auto condition) {
      const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(8);
      while (!condition()) {
        chooser.update(800, 600);
        if (std::chrono::steady_clock::now() > end) throw std::runtime_error("Skin HTTP timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    };
    pump([&] { return chooser.ready(); });
    chooser.update(800, 600);
    const auto lp = chooser.list_position();
    chooser.pointer_down({lp.x + 350, lp.y + 90});
    chooser.pointer_up({lp.x + 350, lp.y + 90});
    if (!chooser.purchasing()) throw std::runtime_error("Source Buy button did not start native HTTP");
    if (mode == "stale" || mode == "other") {
      const auto data = chooser.data();
      chooser.close();
      if (mode == "other") chooser.endpoint("localhost", port, {{"username", "Other"}, {"password", "other-fixture"}});
      chooser.open(775, 835, data, true);
    }
    pump([&] { return !chooser.purchasing(); });
    auto receipt = chooser.take_purchase();
    if (!receipt || receipt->id != 836 || receipt->username != "Fixture")
      throw std::runtime_error("Purchase receipt identity wrong");
    if (mode == "failure") {
      if (receipt->error != "Account in use!" || chooser.owned(836) || !chooser.modal_input() ||
          chooser.data().credits != 1000)
        throw std::runtime_error("Purchase failure changed ownership or hid error");
    } else if (mode == "other") {
      if (chooser.data().owned_skins != std::vector<int>{835} || chooser.data().credits != 1000)
        throw std::runtime_error("Old account purchase leaked into new account");
    } else {
      if (!receipt->error.empty() || chooser.data().credits != 0 ||
          std::find(chooser.data().owned_skins.begin(), chooser.data().owned_skins.end(), 836) ==
              chooser.data().owned_skins.end())
        throw std::runtime_error("Successful/stale ownership receipt missing");
      if (mode == "success" && (!chooser.owned(836) || chooser.selected() != 836))
        throw std::runtime_error("Successful skin not selected");
      if (mode == "stale" && (chooser.selected() != 835 || chooser.rows().size() != 2))
        throw std::runtime_error("Back/reopen changed another class selection");
    }
    std::cout << "Native skin HTTP " << mode << " passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
