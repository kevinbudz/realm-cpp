#include "realm/ui/Trade.hpp"
#include "realm/ui/dialogs/Dialog.hpp"
#include "realm/ui/menu/PlayerMenu.hpp"
#include "realm/messaging/Session.hpp"
#include "flash_assets.hpp"
#include "realm/objects/Movement.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace {
int value(std::string_view input) {
  std::size_t count{};
  const auto number = std::stoi(std::string(input), &count, 0);
  if (count != input.size()) {
    throw std::invalid_argument("Invalid number");
  }
  return number;
}
}

int main(int argc, char** argv) {
  try {
    realm::SessionOptions options;
    int seconds = 10;
    bool password_stdin{};
    bool exercise{};
    bool exercise_inventory{};
    bool exercise_potion{};
    bool exercise_portal{};
    bool exercise_menu{};
    bool exercise_purchase{};
    std::string trade_partner;
    bool trade_initiator{};
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      const auto next = [&] {
        if (++i >= argc) {
          throw std::runtime_error("Missing option value");
        }
        return std::string(argv[i]);
      };
      if (argument == "--server") {
        options.host = next();
      } else if (argument == "--port") {
        const auto port = value(next());
        if (port < 1 || port > 65535) {
          throw std::runtime_error("Invalid port");
        }
        options.port = static_cast<std::uint16_t>(port);
      } else if (argument == "--username") {
        options.username = next();
      } else if (argument == "--character") {
        options.character_id = value(next());
      } else if (argument == "--create") {
        options.create_character = true;
        const auto type = value(next());
        if (type < 0 || type > 65535) {
          throw std::runtime_error("Invalid class type");
        }
        options.class_type = static_cast<std::uint16_t>(type);
      } else if (argument == "--game") {
        options.game_id = value(next());
      } else if (argument == "--seconds") {
        seconds = value(next());
      } else if (argument == "--password-stdin") {
        password_stdin = true;
      } else if (argument == "--exercise") {
        exercise = true;
      } else if (argument == "--exercise-inventory") {
        exercise_inventory = true;
      } else if (argument == "--exercise-potion") {
        exercise_potion = true;
      } else if (argument == "--exercise-portal") {
        exercise_portal = true;
      } else if (argument == "--exercise-menu") {
        exercise_menu = true;
      } else if (argument == "--exercise-purchase") {
        exercise_purchase = true;
      } else if (argument == "--exercise-trade") {
        trade_partner = next();
      } else if (argument == "--trade-initiator") {
        trade_initiator = true;
      } else {
        throw std::runtime_error(
            "Usage: realm_session [--server HOST] [--port N] [--username NAME] [--password-stdin] "
            "[--character "
            "N|--create TYPE] [--game N] [--seconds N]");
      }
    }

    if (seconds < 1 || seconds > 86400) {
      throw std::runtime_error("Invalid session duration");
    }
    if (static_cast<int>(exercise) + exercise_inventory + exercise_potion + exercise_portal +
            exercise_menu + exercise_purchase >
        1) {
      throw std::runtime_error("Choose one exercise mode");
    }

    if (password_stdin) {
      std::getline(std::cin, options.password);
    }

    realm::Catalog catalog(realm::flash::source_root);
    realm::World world(catalog);
    realm::Session session(world);
    session.connect(std::move(options));
    const auto epoch = std::chrono::steady_clock::now();
    auto previous = epoch;
    bool playing{};
    std::int32_t ability_time{};
    int inventory_stage{};
    int source_slot{-1};
    int target_slot{-1};
    realm::InventoryItem original_item;
    realm::InventoryItem dropped_item;
    int potion_stage{};
    int potion_start{};
    int potion_results{};
    int potion_expected{1};
    int portal_stage{};
    std::int32_t portal_id{-1};
    // PlayerMenu actions against a second fixture player.
    // TradePanel flow between two fixture players: request, offer one item, accept.
    realm::Dialog purchase_dialog;
    int purchase_stage{};
    std::int32_t purchase_id{-1};
    std::unique_ptr<realm::FontBook> dialog_fonts;
    if (exercise_purchase) {
      dialog_fonts = std::make_unique<realm::FontBook>();
    }
    realm::Trade trade;
    int trade_stage{};
    int trade_offered{-1};
    int trade_item{-1};
    bool trade_changed_seen{};
    int menu_stage{};
    int menu_lists{};
    std::int32_t menu_target{-1};
    std::int32_t menu_account{-1};
    bool menu_goto{};
    bool menu_trade_reply{};
    while (std::chrono::steady_clock::now() - epoch < std::chrono::seconds(seconds)) {
      const auto now = std::chrono::steady_clock::now();
      const auto time = static_cast<std::int32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now - epoch).count());
      const auto dt = std::chrono::duration<double, std::milli>(now - previous).count();
      previous = now;
      session.poll(time);
      world.update(dt);
      if (dialog_fonts && purchase_stage == 0 && session.state() == realm::SessionState::playing) {
        if (const auto* player = world.entity(world.player_id())) {
          for (const auto& entity : world.entities()) {
            if (const auto* def = catalog.object(entity.type);
                def && def->class_name == "Merchant") {
              const auto price =
                  entity.profile.stats.value_or(realm::flash::StatId::MERCHANDISE_PRICE_STAT, -1);
              const auto currency = entity.profile.stats.value_or(
                  realm::flash::StatId::MERCHANDISE_CURRENCY_STAT, -1);
              const auto gold =
                  player->profile.stats.value_or(realm::flash::StatId::CREDITS_STAT, -1);
              if (currency == 0 && price > gold) {
                purchase_id = entity.id;
                session.buy(purchase_id);
                session.buy(purchase_id);
                purchase_stage = 1;
                break;
              }
            }
          }
        }
      }
      if (exercise_inventory && inventory_stage == 0 &&
          session.state() == realm::SessionState::playing) {
        if (const auto* player = world.entity(world.player_id())) {
          for (int i = 4; i < 12; ++i) {
            const auto item = realm::inventory_item(*player, i);
            if (item.type == -1 && target_slot < 0) {
              target_slot = i;
            }
            if (item.type > 0 && item.type <= 65535) {
              if (const auto* def = catalog.object(static_cast<std::uint16_t>(item.type));
                  def && def->consumable && source_slot < 0) {
                source_slot = i;
                original_item = item;
              }
            }
          }
          if (source_slot >= 0 && target_slot >= 0) {
            session.swap_items({player->id, static_cast<std::uint8_t>(source_slot)},
                               {player->id, static_cast<std::uint8_t>(target_slot)}, time);
            inventory_stage = 1;
          }
        }
      }
      // The dropped weapon returns in a soulbound bag; pick it up as a
      // ContainerGrid double-click would, then re-equip it.
      if (exercise_inventory && inventory_stage == 4) {
        if (const auto* player = world.entity(world.player_id())) {
          if (const auto* bag = world.interactive_target()) {
            const auto* def = catalog.object(bag->type);
            for (int i = 0; def && def->container && i < 8; ++i) {
              if (realm::inventory_item(*bag, i) == dropped_item) {
                session.equip_or_use({bag->id, static_cast<std::uint8_t>(i)}, time);
                for (int j = 4; j < 12; ++j) {
                  if (realm::inventory_item(*player, j) == dropped_item) {
                    target_slot = j;
                  }
                }
                if (target_slot < 0) {
                  throw std::runtime_error("Container pickup was not predicted");
                }
                inventory_stage = 5;
                break;
              }
            }
          }
        }
      }
      // Stat updates queued before an accepted swap can arrive ahead of its
      // INVRESULT, so the exercise waits for the authoritative slots.
      if (exercise_inventory && inventory_stage == 6) {
        if (const auto* player = world.entity(world.player_id());
            player && realm::inventory_item(*player, target_slot) == dropped_item) {
          session.equip_or_use({player->id, static_cast<std::uint8_t>(target_slot)}, time);
          if (realm::inventory_item(*player, 0) != dropped_item) {
            throw std::runtime_error("Re-equip was not predicted");
          }
          inventory_stage = 7;
        }
      }
      if (exercise_inventory && inventory_stage == 8) {
        if (const auto* player = world.entity(world.player_id());
            player && realm::inventory_item(*player, 0) == dropped_item) {
          inventory_stage = 9;
          std::cout << "Real server inventory SWAP, consumable USEITEM, DROP, container pickup and "
                       "re-equip accepted\n";
        }
      }
      // Spend MP with the starter ability unless an earlier session left it
      // below maximum, then refill it from the magic stack.
      if (exercise_potion && session.state() == realm::SessionState::playing) {
        if (const auto* player = world.entity(world.player_id())) {
          if (potion_stage == 0 && player->profile.stats.value_or(60, -1) > 0 &&
              player->profile.stats.value_or(3, -1) > 0) {
            potion_start = player->profile.stats.value_or(60, -1);
            if (player->profile.stats.value_or(4, -1) >= player->profile.stats.value_or(3, -1)) {
              session.use_ability({player->motion.position.x + 3, player->motion.position.y}, time);
              potion_expected = 2;
            }
            potion_stage = 1;
          } else if (potion_stage == 1 && player->profile.stats.value_or(4, -1) <
                                              player->profile.stats.value_or(3, -1)) {
            session.use_potion(1, time);
            if (player->profile.stats.value_or(60, -1) != potion_start - 1) {
              throw std::runtime_error("Magic potion was not predicted");
            }
            potion_stage = 2;
          } else if (potion_stage == 3 &&
                     player->profile.stats.value_or(4, -1) ==
                         player->profile.stats.value_or(3, -1) &&
                     player->profile.stats.value_or(60, -1) == potion_start - 1) {
            potion_stage = 4;
            std::cout << "Real server magic potion USEITEM accepted; stack " << potion_start
                      << " -> " << potion_start - 1 << "\n";
          }
        }
      }
      // Walk to the Nexus vault portal until its PortalPanel would show, then enter it.
      if (exercise_portal && session.state() == realm::SessionState::playing) {
        if (auto* player = world.entity(world.player_id())) {
          if (portal_stage == 0) {
            double best = 1e9;
            for (const auto& entity : world.entities()) {
              if (const auto* def = catalog.object(entity.type);
                  def && def->class_name == "Portal" && def->id == "Vault Portal") {
                const double distance =
                    std::hypot(entity.motion.position.x - player->motion.position.x,
                               entity.motion.position.y - player->motion.position.y);
                if (distance < best) {
                  best = distance;
                  portal_id = entity.id;
                }
              }
            }
            if (portal_id >= 0) {
              portal_stage = 1;
            }
          } else if (portal_stage == 1) {
            const auto* portal = world.entity(portal_id);
            if (!portal) {
              throw std::runtime_error("Vault portal disappeared");
            }
            if (const auto* target = world.interactive_target();
                target && target->id == portal_id) {
              const auto active = target->profile.stats.find(
                  static_cast<std::uint8_t>(realm::flash::StatId::ACTIVE_STAT));
              if (active != target->profile.stats.end() && active->second == 0) {
                throw std::runtime_error("Vault portal is inactive");
              }
              std::cout << "Interaction target "
                        << (target->profile.name.empty() ? catalog.object(target->type)->display_id
                                                         : target->profile.name)
                        << " entered\n";
              session.use_portal(portal_id);
              portal_stage = 2;
            } else {
              double angle{};
              const double dx = portal->motion.position.x - player->motion.position.x;
              const double dy = portal->motion.position.y - player->motion.position.y;
              const double length = std::hypot(dx, dy);
              realm::move_player(world, *player, {dx / length, dy / length}, 0, dt, angle);
            }
          } else if (portal_stage == 2 && world.name() != "Nexus") {
            portal_stage = 3;
            std::cout << "Real server USEPORTAL reconnected to map=" << world.name() << "\n";
          }
        }
      }
      if (exercise && session.state() == realm::SessionState::playing) {
        if (auto* player = world.entity(world.player_id())) {
          double angle{};
          realm::move_player(world, *player, {time % 2000 < 1000 ? 1.0 : -1.0, 0}, 0, dt, angle);
          const realm::Vec2 ability_target{player->motion.position.x + 3,
                                           player->motion.position.y};
          session.shoot(0, time);
          if (time >= ability_time) {
            session.use_ability(ability_target, time);
            ability_time = time + 2000;
          }
        }
      }
      if (exercise_menu && session.state() == realm::SessionState::playing && menu_stage == 0) {
        // Players arrive with 3 seconds of server-applied invisibility.
        for (const auto& player : world.entities()) {
          if (player.player && player.id != world.player_id() &&
              !(player.combat.condition & realm::invisible_condition)) {
            menu_target = player.id;
            menu_account = player.profile.stats.value_or(realm::flash::StatId::ACCOUNT_ID_STAT, -1);
            break;
          }
        }
        if (menu_target >= 0) {
          const auto target = realm::player_menu_target(world, session.party(), menu_target,
                                                        session.map_info().allow_teleport);
          using Action = realm::PlayerMenuAction;
          if (!target || target->options != std::vector{Action::teleport, Action::lock,
                                                        Action::trade, Action::ignore}) {
            std::string found;
            if (target) {
              for (const auto option : target->options) {
                found += std::to_string(static_cast<int>(option)) + ",";
              }
            }
            throw std::runtime_error(
                "Unexpected PlayerMenu options for fixture player: " + (target ? found : "none") +
                " allow_teleport=" + std::to_string(session.map_info().allow_teleport));
          }
          if (session.teleport(menu_target, time)) {
            throw std::runtime_error("First teleport was refused");
          }
          const auto again = session.teleport(menu_target, time);
          if (!again ||
              again->find("You can not teleport for another 11 seconds.") == std::string::npos) {
            throw std::runtime_error("Teleport cooldown not enforced");
          }
          session.edit_account_list(menu_target, false, true);
          session.request_trade(target->name);
          menu_stage = 1;
        }
      }
      if (exercise_menu && menu_stage == 1 && menu_goto && menu_lists == 1 && menu_trade_reply &&
          session.party().starred(menu_target)) {
        if (const auto again = player_menu_target(world, session.party(), menu_target, true);
            !again || again->options.at(1) != realm::PlayerMenuAction::unlock) {
          throw std::runtime_error("Locked player does not offer Unlock");
        }
        session.edit_account_list(menu_target, false, false);
        session.edit_account_list(menu_target, true, true);
        menu_stage = 2;
      }
      if (exercise_menu && menu_stage == 2 && menu_lists == 3) {
        if (!session.party().ignored(menu_account)) {
          throw std::runtime_error("Ignore list was not applied");
        }
        session.edit_account_list(menu_target, true, false);
        menu_stage = 3;
      }
      if (exercise_menu && menu_stage == 3 && menu_lists == 4 &&
          !session.party().ignored(menu_account)) {
        menu_stage = 4;
        std::cout << "Real server PlayerMenu TELEPORT, EDITACCOUNTLIST lock/unlock/ignore/unignore "
                     "and REQUESTTRADE "
                     "accepted\n";
        session.player_text("native chat check");
      }
      if (!trade_partner.empty() && session.state() == realm::SessionState::playing) {
        trade.update(time);
        if (trade_stage == 0 && trade_initiator) {
          for (const auto& player : world.entities()) {
            if (player.player && player.profile.name == trade_partner &&
                !(player.combat.condition & realm::invisible_condition)) {
              session.request_trade(trade_partner);
              trade_stage = 1;
              break;
            }
          }
        }
        // Accept once the countdown allows it, after the offer is settled on both sides.
        // Fresh fixtures hold only soulbound gear; an empty trade still runs the full flow.
        if (trade_stage == 2 && trade.button() == realm::Trade::Button::normal &&
            (trade_initiator || trade_changed_seen || trade_item < 0)) {
          if (const auto offers = trade.accept(time)) {
            session.accept_trade(offers->first, offers->second);
            trade_stage = 3;
          }
        }
      }
      session.finish_frame(time);
      for (const auto& event : session.take_events()) {
        if (!trade_partner.empty()) {
          using PacketId = realm::flash::PacketId;
          if (event.id == PacketId::TRADEREQUESTED && !trade_initiator) {
            session.request_trade(std::get<realm::net::incoming::TradeRequested>(event.data).name);
            trade_stage = 1;
          }
          if (event.id == PacketId::TRADESTART) {
            trade.start(std::get<realm::net::incoming::TradeStart>(event.data), "", time);
            trade_stage = 2;
            if (trade_initiator) {
              for (std::size_t i = 4; i < realm::Trade::slots; ++i) {
                if (trade.mine().items[i].tradeable && trade.mine().items[i].item > 0) {
                  trade_item = trade.mine().items[i].item;
                  if (const auto offer = trade.toggle(i, time)) {
                    session.change_trade(*offer);
                    trade_offered = static_cast<int>(i);
                  }
                  break;
                }
              }
            }
          }
          if (event.id == PacketId::TRADECHANGED) {
            trade.changed(std::get<realm::net::incoming::TradeChanged>(event.data), time);
            trade_changed_seen = true;
          }
          if (event.id == PacketId::TRADEACCEPTED) {
            trade.accepted(std::get<realm::net::incoming::TradeAccepted>(event.data));
          }
          if (event.id == PacketId::TRADEDONE) {
            const auto& done = std::get<realm::net::incoming::TradeDone>(event.data);
            trade.end();
            if (done.text != "Trade Successful!") {
              throw std::runtime_error("Trade ended: " + done.text);
            }
            trade_stage = 4;
            std::cout << "Real server trade completed: " << done.text << '\n';
          }
        }
        if (exercise_menu && event.id == realm::flash::PacketId::GOTO &&
            std::get<realm::net::incoming::Goto>(event.data).object_id == world.player_id()) {
          menu_goto = true;
        }
        if (exercise_menu && event.id == realm::flash::PacketId::ACCOUNTLIST) {
          const auto& list = std::get<realm::net::incoming::AccountList>(event.data);
          const bool listed = std::find(list.account_ids.begin(), list.account_ids.end(),
                                        menu_account) != list.account_ids.end();
          // Lock, unlock, ignore and unignore each return the updated list.
          const std::array expected{std::pair{0, true}, std::pair{0, false}, std::pair{1, true},
                                    std::pair{1, false}};
          if (menu_stage >= 1 && menu_lists < 4) {
            if (list.list_id != expected[static_cast<std::size_t>(menu_lists)].first ||
                listed != expected[static_cast<std::size_t>(menu_lists)].second) {
              throw std::runtime_error("Unexpected ACCOUNTLIST reply");
            }
            ++menu_lists;
          }
        }
        if (exercise_menu && event.id == realm::flash::PacketId::TEXT) {
          const auto& message = std::get<realm::net::incoming::Text>(event.data);
          const auto& text = message.text;
          if (menu_stage == 4 && text == "native chat check" &&
              message.object_id == world.player_id()) {
            menu_stage = 5;
            std::cout << "Real server PLAYERTEXT echoed from " << message.name << "\n";
          }
          // New fixture accounts have no chosen name, which the server checks before sending the
          // request.
          if (text.find("unique name is required before trading") != std::string::npos ||
              text.find("sent a trade request") != std::string::npos) {
            menu_trade_reply = true;
          }
        }
        if (dialog_fonts && event.id == realm::flash::PacketId::BUYRESULT) {
          const auto& result = std::get<realm::net::incoming::BuyResult>(event.data);
          if (purchase_stage != 1 || session.buy_pending() ||
              !purchase_dialog.purchase_result(result) ||
              result.text != "Purchase Error: Insufficient Funds.") {
            throw std::runtime_error("Unexpected live purchase error");
          }
          dialog_fonts->set_pixel_scale(1.2);
          purchase_dialog.update(*dialog_fonts, 1280, 720);
          if (!purchase_dialog.visible() || purchase_dialog.content().title != "Purchase Error" ||
              purchase_dialog.height() != 100) {
            throw std::runtime_error("Live Purchase Error layout failed");
          }
          const auto button = purchase_dialog.button(0);
          const auto position = purchase_dialog.position();
          const realm::Vec2 point{position.x + (button.x + 10) * purchase_dialog.scale(),
                                  position.y + (button.y + 10) * purchase_dialog.scale()};
          purchase_dialog.pointer_down(point);
          if (purchase_dialog.pointer_up(point) != 0 || purchase_dialog.visible()) {
            throw std::runtime_error("Live Purchase Error close failed");
          }
          purchase_stage = 2;
          std::cout << "Real server BUYRESULT 1 opened and closed native Purchase Error; pending "
                       "purchase released\n";
        }
        std::cout << "packet=" << static_cast<int>(event.id) << '\n';
        if (event.id == realm::flash::PacketId::TEXT) {
          std::cout << std::get<realm::net::incoming::Text>(event.data).text << '\n';
        }
        if (exercise_potion && event.id == realm::flash::PacketId::INVRESULT) {
          if (std::get<realm::net::incoming::InvResult>(event.data).result != 0) {
            throw std::runtime_error("Real server rejected potion exercise");
          }
          if (++potion_results == potion_expected && potion_stage == 2) {
            potion_stage = 3;
          }
        }
        if (exercise_inventory && event.id == realm::flash::PacketId::INVRESULT) {
          if (std::get<realm::net::incoming::InvResult>(event.data).result != 0) {
            throw std::runtime_error("Real server rejected inventory exercise");
          }
          const auto* player = world.entity(world.player_id());
          if (!player) {
            throw std::runtime_error("Inventory fixture player disappeared");
          }
          if (inventory_stage == 1) {
            if (realm::inventory_item(*player, target_slot) != original_item) {
              throw std::runtime_error("Real server swap differs from prediction");
            }
            session.use_item({player->id, static_cast<std::uint8_t>(target_slot)}, time);
            inventory_stage = 2;
          } else if (inventory_stage == 2) {
            if (realm::inventory_item(*player, target_slot).type != -1) {
              throw std::runtime_error("Real server consumable differs from prediction");
            }
            if (realm::inventory_item(*player, 0).type == -1) {
              throw std::runtime_error("Isolated fixture needs starter weapon");
            }
            dropped_item = realm::inventory_item(*player, 0);
            session.drop_to_ground(0, time);
            inventory_stage = 3;
          } else if (inventory_stage == 3) {
            if (realm::inventory_item(*player, 0).type != -1) {
              throw std::runtime_error("Real server drop differs from prediction");
            }
            inventory_stage = 4;
            target_slot = -1;
          } else if (inventory_stage == 5 || inventory_stage == 7) {
            ++inventory_stage;
          }
        }
      }
      if (session.state() == realm::SessionState::playing && !playing) {
        std::cout << "Playing character=" << session.character_id() << " map=" << world.name()
                  << '\n';
        playing = true;
      }
      if (session.state() == realm::SessionState::failed ||
          session.state() == realm::SessionState::disconnected) {
        throw std::runtime_error(session.error());
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (dialog_fonts && purchase_stage != 2) {
      throw std::runtime_error("Purchase exercise incomplete at stage " +
                               std::to_string(purchase_stage));
    }
    if (exercise_inventory && inventory_stage != 9) {
      throw std::runtime_error("Inventory exercise incomplete at stage " +
                               std::to_string(inventory_stage));
    }
    if (exercise_portal && portal_stage != 3) {
      throw std::runtime_error("Portal exercise incomplete at stage " +
                               std::to_string(portal_stage));
    }
    if (!trade_partner.empty()) {
      if (trade_stage != 4) {
        throw std::runtime_error("Trade exercise incomplete at stage " +
                                 std::to_string(trade_stage));
      }
      // The traded item moved: the initiator no longer holds it in the offered slot.
      if (trade_initiator && trade_offered >= 0) {
        const auto* player = world.entity(world.player_id());
        if (!player || realm::inventory_item(*player, trade_offered).type == trade_item) {
          throw std::runtime_error("Traded item did not leave the inventory");
        }
        std::cout << "Trade inventory verified\n";
      }
    }
    if (exercise_menu && menu_stage != 5) {
      throw std::runtime_error("PlayerMenu exercise incomplete at stage " +
                               std::to_string(menu_stage));
    }
    if (exercise_potion && potion_stage != 4) {
      throw std::runtime_error("Potion exercise incomplete at stage " +
                               std::to_string(potion_stage));
    }
    session.disconnect();
    std::cout << "World " << world.width() << 'x' << world.height()
              << " objects=" << world.entities().size() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "realm_session: " << error.what() << '\n';
    return 1;
  }
}
