#include "realm/interaction.hpp"
#include "flash_assets.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
int checks{};
void check(bool condition, const char* name) {
  ++checks;
  if (!condition) throw std::runtime_error(name);
}
}
int main(int argc, char** argv) {
  try {
    if (argc > 2) throw std::runtime_error("Expected no arguments or an output directory");
    using K = realm::HudState::SocialKind;
    realm::Catalog catalog(realm::flash::source_root);
    realm::World world(catalog);
    world.initialize(8, 8, "fixture");
    const auto* wizard = catalog.object("Wizard");
    check(wizard != nullptr, "Wizard exists");
    world.add(99, wizard->type, {3, 3});
    world.set_player_id(99);
    world.entity(99)->profile.name = "Original";
    const auto target = [&](std::string_view class_name) {
      world.remove(77);
      for (const auto& def : catalog.objects())
        if (def.class_name == class_name) {
          world.add(77, def.type, {3, 3.2});
          return;
        }
      throw std::runtime_error("Missing interaction definition");
    };
    realm::FontBook fonts;
    realm::InteractionDialog dialog;
    target("GuildHallPortal");
    auto state = realm::HudState::player(world, *world.entity(99));
    check(state.social.kind == K::hall && !state.social.enabled && state.social.notice == "Not In Guild",
          "hall blocks nonguild player");
    world.entity(99)->profile.guild = "Native Guild";
    state = realm::HudState::player(world, *world.entity(99));
    check(state.social.enabled && state.social.id == 77, "hall allows guild player");
    check(!dialog.open(state.social, *world.entity(99)), "hall has no modal");
    target("GuildRegister");
    world.entity(99)->profile.stats[57] = 40;
    state = realm::HudState::player(world, *world.entity(99));
    check(state.social.kind == K::leave_guild && state.social.title == "Founder of\nNative Guild",
          "registrar membership title");
    check(dialog.open(state.social, *world.entity(99)) && dialog.visible() && !dialog.wants_text(),
          "leave confirmation");
    dialog.pointer_down({240, 305});
    dialog.pointer_up({10, 10});
    check(!dialog.pending(), "release outside does not renounce");
    dialog.pointer_down({240, 305});
    dialog.focus_lost();
    dialog.pointer_up({240, 305});
    check(!dialog.pending(), "focus loss cancels press");
    dialog.cancel();
    check(!dialog.visible(), "cancel leaves membership alone");
    (void)dialog.open(state.social, *world.entity(99));
    dialog.submit();
    auto request = dialog.take_request();
    check(request && request->kind == K::leave_guild && request->value == "Original", "leave addresses self");
    dialog.submit();
    check(!dialog.take_request() && dialog.pending(), "pending submit deduplicates");
    dialog.cancel();
    check(dialog.visible(), "pending cannot cancel response tracking");
    realm::net::incoming::Text error;
    error.name = "*Error*";
    error.text = "Guild not found.";
    dialog.receive({realm::flash::PacketId::TEXT, error});
    check(!dialog.pending() && dialog.error() == error.text, "renounce error surfaced");
    dialog.submit();
    (void)dialog.take_request();
    world.entity(99)->profile.guild.clear();
    dialog.update(world);
    check(!dialog.visible(), "authoritative departure closes modal");
    state = realm::HudState::player(world, *world.entity(99));
    check(state.social.kind == K::create_guild && state.social.price == 1000 && state.social.currency == 1,
          "create guild price");
    (void)dialog.open(state.social, *world.entity(99));
    dialog.submit();
    check(!dialog.pending() && !dialog.error().empty(), "empty name rejected locally");
    dialog.text("Native 12! Guild");
    check(dialog.value() == "Native  Guild", "guild input letters and spaces only");
    dialog.text("abcdefghijklmnopqrstuvwxyz");
    check(dialog.value().size() == 20, "guild input limit");
    dialog.backspace();
    check(dialog.value().size() == 19, "backspace");
    dialog.submit();
    request = dialog.take_request();
    check(request && request->kind == K::create_guild && world.entity(99)->profile.guild.empty(),
          "guild creation remains authoritative");
    dialog.receive({realm::flash::PacketId::NAMERESULT, realm::net::incoming::NameResult{true, ""}});
    check(dialog.pending(), "unrelated result ignored");
    dialog.receive(
        {realm::flash::PacketId::GUILDRESULT, realm::net::incoming::GuildResult{false, "Insufficient funds"}});
    check(dialog.visible() && !dialog.pending() && dialog.error() == "Insufficient funds",
          "guild rejection enables retry");
    dialog.submit();
    (void)dialog.take_request();
    dialog.receive({realm::flash::PacketId::GUILDRESULT, realm::net::incoming::GuildResult{true, "Success"}});
    check(!dialog.visible(), "guild success closes");
    target("NameChanger");
    state = realm::HudState::player(world, *world.entity(99));
    check(state.social.kind == K::choose_name && state.social.enabled, "first name selection");
    (void)dialog.open(state.social, *world.entity(99));
    dialog.text("New Name123!abcdefghijk");
    check(dialog.value() == "NewNameabc", "name filter and limit");
    dialog.submit();
    request = dialog.take_request();
    check(request && request->kind == K::choose_name && world.entity(99)->profile.name == "Original",
          "name request does not predict player stats");
    dialog.receive({realm::flash::PacketId::NAMERESULT, realm::net::incoming::NameResult{false, "Not enough gold."}});
    check(dialog.error() == "Not enough gold." && !dialog.pending(), "name rejection visible");
    dialog.submit();
    (void)dialog.take_request();
    dialog.receive({realm::flash::PacketId::NAMERESULT, realm::net::incoming::NameResult{true, ""}});
    check(!dialog.visible() && dialog.take_name() == std::optional<std::string>{"NewNameabc"} && !dialog.take_name(),
          "accepted name handed off once");
    world.entity(99)->profile.stats[91] = 1;
    state = realm::HudState::player(world, *world.entity(99));
    check(state.social.kind == K::change_name && state.social.currency == 0 && state.social.price == 1000,
          "change name price");
    (void)dialog.open(state.social, *world.entity(99));
    dialog.text("Changed");
    dialog.submit();
    (void)dialog.take_request();
    dialog.receive({realm::flash::PacketId::RECONNECT, realm::net::incoming::Reconnect{-2}});
    check(!dialog.visible() && !dialog.take_request(), "reconnect clears modal");
    for (const auto window : std::array<realm::Vec2, 3>{{{800, 600}, {1280, 720}, {320, 240}}}) {
      const auto p = realm::InteractionDialog::position(window.x, window.y);
      const auto scale = realm::InteractionDialog::scale(window.x, window.y);
      const auto local = realm::InteractionDialog::local({p.x + 240 * scale, p.y + 305 * scale}, window.x, window.y);
      check(std::abs(local.x - 240) < 1e-6 && std::abs(local.y - 305) < 1e-6, "resized modal picking");
      check(p.x >= 0 && p.y >= 0 && p.x + 440 * scale <= window.x && p.y + 340 * scale <= window.y,
            "modal fits window");
    }
    if (argc > 1) {
      realm::HudArt art;
      const std::filesystem::path output = argv[1];
      std::filesystem::create_directories(output);
      realm::Hud hud(fonts, &art);
      for (const auto cls : {"GuildHallPortal", "GuildRegister", "NameChanger"})
        for (int member = 0; member < 2; ++member) {
          target(cls);
          world.entity(99)->profile.guild = member ? "Native Guild" : "";
          world.entity(99)->profile.stats[91] = member;
          state = realm::HudState::player(world, *world.entity(99));
          if (cls == std::string_view("GuildRegister")) world.entity(99)->profile.stats[57] = 40;
          check(hud.update(state), "social panel transition redraws");
          realm::Hud fresh(fonts, &art);
          (void)fresh.update(state);
          check(hud.image().pixels == fresh.image().pixels, "incremental panel matches fresh render");
          const auto count = hud.rebuilds();
          for (int i = 0; i < 1000; ++i) check(!hud.update(state), "unchanged panel reused");
          check(count == hud.rebuilds(), "no unchanged rebuilds");
          realm::save_png(output / (std::string(cls) + std::to_string(member) + ".png"), hud.image());
          dialog.reset();
          if (dialog.open(state.social, *world.entity(99))) {
            dialog.text("Native");
            check(dialog.render(fonts), "modal rasterizes");
            const auto builds = dialog.rebuilds();
            for (int i = 0; i < 1000; ++i) check(!dialog.render(fonts), "unchanged modal reused");
            check(builds == dialog.rebuilds(), "modal cache stable");
            realm::save_png(output / (std::string(cls) + std::to_string(member) + "-dialog.png"), dialog.image());
            dialog.submit();
            (void)dialog.take_request();
            dialog.receive(
                {realm::flash::PacketId::NAMERESULT,
                 realm::net::incoming::NameResult{false, "This name is already taken. Please choose another name."}});
            (void)dialog.render(fonts);
            realm::save_png(output / (std::string(cls) + std::to_string(member) + "-error.png"), dialog.image());
          }
          const auto button = state.panel_button(fonts);
          check((button.id >= 0) == state.social.enabled, "social button visibility");
        }
      target("GuildRegister");
      world.entity(99)->profile.guild = std::string(20, 'W');
      world.entity(99)->profile.stats[57] = 40;
      state = realm::HudState::player(world, *world.entity(99));
      check(hud.update(state), "wide guild title renders");
      realm::save_png(output / "wide-guild.png", hud.image());
      std::cout << "Rendered social panels and modals; retained caches verified\n";
    }
    std::cout << "Interaction checks: " << checks << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
