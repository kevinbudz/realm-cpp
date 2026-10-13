#include "ApplicationLoop.hpp"
#include "realm/util/Telemetry.hpp"
#include "realm/messaging/GameServerConnection.hpp"
#include "realm/ui/Chat.hpp"
#include "realm/ui/guild/GuildPlayerList.hpp"
#include "realm/lib/ui/UIStack.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <memory>
#include <stdexcept>
#include <string>

#include "ApplicationState.hpp"

namespace realm::application {
void update_session(ClientState& cs, Frame& f) {
  auto& dt = f.dt;
  auto& time = f.time;
  if (cs.terrain) cs.terrain->begin_frame();
  if (cs.startup)
    if (auto launch = cs.startup->take_launch()) {
      cs.game_session = std::make_unique<realm::Session>(cs.world);
      cs.background_world = false;
      try {
        cs.game_session->connect(std::move(*launch));
      } catch (const std::exception& e) {
        cs.game_session.reset();
        cs.startup->left_game(e.what());
        cs.startup->load_background(cs.world);
        cs.background_world = true;
      }
    }
  if (cs.game_session) {
    cs.game_session->poll(time);
    cs.world.update(dt * 1000);
    for (const auto& message : cs.game_session->take_events()) {
      // A new map drops plays still waiting for a load (plays never outlive their map's objects).
      if (message.id == realm::flash::PacketId::MAPINFO) cs.audio_bridge.map_changed();
      if (message.id == realm::flash::PacketId::MAPINFO || message.id == realm::flash::PacketId::RECONNECT ||
          message.id == realm::flash::PacketId::DEATH) {
        cs.ui_stack.reset(realm::ui::Reset::map_change);
      }
      cs.interaction.receive(message);
      if (auto name = cs.interaction.take_name(); name && cs.startup) cs.startup->renamed_account(*name);
      // onBuyResult: result 1 opens a "Purchase Error" dialog; other
      // results become chat lines (server color on success, otherwise *Error*).
      if (message.id == realm::flash::PacketId::BUYRESULT) {
        const auto& result = std::get<realm::net::incoming::BuyResult>(message.data);
        if (cs.dialog.purchase_result(result)) {
          // The dialog is modal: the stack cancels presses, drags and hover beneath it; these menus close.
          cs.chat.close();
          if (cs.player_menu) cs.player_menu->close();
          if (cs.player_group) cs.player_group->close();
          cs.party_arrows.reset();
          cs.ui_stack.update();
        } else
          cs.chat.add(std::string(result.result == 0 ? realm::server_chat_name : realm::error_chat_name), result.text,
                      time);
      }
      switch (message.id) {
        case realm::flash::PacketId::TRADEREQUESTED: {
          const auto& name = std::get<realm::net::incoming::TradeRequested>(message.data).name;
          if (cs.trade_view && !cs.trade.active() && cs.settings.flag("showTradePopup")) cs.trade.requested(name, time);
          cs.chat.add("", name + " wants to trade with you.  Type \"/trade " + name + "\" to trade.", time);
          break;
        }
        case realm::flash::PacketId::TRADESTART:
          cs.trade.start(std::get<realm::net::incoming::TradeStart>(message.data), cs.own_name(), time);
          cs.trade_pressed = -1;
          break;
        case realm::flash::PacketId::TRADECHANGED:
          cs.trade.changed(std::get<realm::net::incoming::TradeChanged>(message.data), time);
          break;
        case realm::flash::PacketId::TRADEACCEPTED:
          cs.trade.accepted(std::get<realm::net::incoming::TradeAccepted>(message.data));
          break;
        case realm::flash::PacketId::TRADEDONE:
          cs.trade.end();
          cs.trade_pressed = -1;
          cs.chat.add("", std::get<realm::net::incoming::TradeDone>(message.data).text, time);
          break;
        default:
          break;
      }
      if (message.id == realm::flash::PacketId::TEXT) {
        const auto& text = std::get<realm::net::incoming::Text>(message.data);
        cs.members.receive(text, cs.world.player_id());
        cs.balloons.add(cs.world, text);
        cs.chat.add(text, cs.own_name(), time);
      }
    }
    const auto state = cs.game_session->state();
    if (cs.startup) {
      if (state == realm::SessionState::playing && cs.startup->menus()) cs.startup->entered_game();
      if (state == realm::SessionState::failed || state == realm::SessionState::disconnected ||
          state == realm::SessionState::dead) {
        const auto death = cs.game_session->death();
        if (death) {
          int snapshot_width{}, snapshot_height{};
          SDL_GL_GetDrawableSize(cs.session.window, &snapshot_width, &snapshot_height);
          cs.death_background = std::make_unique<realm::SurfaceTexture>();
          cs.death_background->update(cs.renderer.capture(snapshot_width, snapshot_height, true));
        }
        std::string message = cs.leaving_game ? "" : cs.game_session->error();
        if (message.empty() && state != realm::SessionState::dead && !cs.leaving_game)
          message = "Disconnected from the game server.";
        // Leaving, disconnecting or dying ends every voice; the death screen's sound is queued after this.
        cs.audio_bridge.stop();
        cs.game_session.reset();
        cs.ui_stack.reset(realm::ui::Reset::session_end);
        cs.leaving_game = false;
        if (death)
          cs.startup->died(*death, time);
        else
          cs.startup->left_game(message);
        cs.startup->load_background(cs.world);
        cs.background_world = true;
      }
    } else if (state == realm::SessionState::failed || state == realm::SessionState::disconnected)
      throw std::runtime_error(cs.game_session->error());
    cs.interaction.update(cs.world);
    if (cs.game_session)
      if (const auto* player = cs.world.entity(cs.world.player_id())) cs.position = player->motion.position;
  }
}

void update_panels(ClientState& cs, Frame& f) {
  auto& dt = f.dt;
  auto& menus = f.menus;
  if (cs.startup)
    if (auto error = cs.startup->take_skin_error())
      cs.dialog.message_close("D'oh, this isn't good", "An error has occured:\n" + *error, "Ok");
  cs.board.poll();
  cs.members.poll(dt);
  if (cs.skins) cs.skins->poll();
  if (cs.members.visible() && cs.game_session)
    if (const auto* player = cs.world.entity(cs.world.player_id()))
      cs.members.permissions(player->profile.guild,
                             player->profile.stats.value_or(realm::flash::StatId::GUILD_RANK_STAT, 0));
  if (auto action = cs.members.take_request(); action && cs.game_session) {
    if (action->action == realm::GuildMembers::Action::remove)
      cs.game_session->leave_guild(action->name);
    else
      cs.game_session->change_guild_rank(action->name, action->rank);
  }
  if (cs.board.visible() && cs.game_session)
    if (const auto* player = cs.world.entity(cs.world.player_id()))
      cs.board.permissions(player->profile.stats.value_or(realm::flash::StatId::GUILD_RANK_STAT, 0));
  menus = cs.startup && cs.startup->menus();
  cs.ui_stack.update();
}

}  // namespace realm::application
