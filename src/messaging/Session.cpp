#include "realm/util/Telemetry.hpp"
#include "realm/messaging/Session.hpp"
#include "realm/objects/Movement.hpp"
#include "flash_assets.hpp"
namespace realm {
bool Session::reskin(int id, std::span<const int> owned) {
  const auto* player = world_.entity(world_.player_id());
  if (state_ != SessionState::playing || !player || id < 0 || id > 65535) return false;
  if (id && (std::find(owned.begin(), owned.end(), id) == owned.end() || !world_.catalog().skin(id, player->type)))
    return false;
  send(net::outgoing::Reskin{id});
  return true;
}

void Session::send_packet(const net::Packet& packet) {
  if (replay_sink_) {
    telemetry::add(telemetry::Counter::packet_tx_bytes, packet.body.size() + 5);
    replay_sink_(packet);
  } else
    transport_.send(packet);
}
void Session::replay(const net::Packet& packet, std::int32_t time) {
  if (!replay_sink_) throw std::logic_error("Session replay requires an offline packet sink");
  REALM_ZONE("network.decode_dispatch");
  telemetry::add(telemetry::Counter::packets_decoded);
  receive(net::decode(packet), time);
}
void Session::connect(SessionOptions options) {
  if (replay_sink_) throw std::logic_error("Offline replay cannot connect a socket");
  world_.particles().clear();
  transport_.close();
  options_ = std::move(options);
  hello_sent_ = false;
  pending_moves_ = 0;
  error_.clear();
  events_.clear();
  inventory_.reset();
  pending_name_.clear();
  buy_pending_ = false;
  death_.reset();
  transport_.connect(options_.host, options_.port);
  state_ = SessionState::connecting;
}
void Session::disconnect() {
  world_.particles().clear();
  transport_.close();
  inventory_.reset();
  pending_name_.clear();
  buy_pending_ = false;
  state_ = SessionState::disconnected;
}
void Session::poll(std::int32_t time) {
  REALM_ZONE("session.poll");
  if (replay_sink_) return;
  if (state_ == SessionState::disconnected || state_ == SessionState::failed || state_ == SessionState::dead) return;
  try {
    auto packets = transport_.poll();
    if (transport_.state() == net::ConnectionState::connected && !hello_sent_) {
      send(net::outgoing::Hello{options_.build_version, options_.game_id, options_.username, options_.password,
                                options_.map_json});
      hello_sent_ = true;
    }
    for (const auto& packet : packets) {
      REALM_ZONE("network.decode_dispatch");
      telemetry::add(telemetry::Counter::packets_decoded);
      receive(net::decode(packet), time);
      if (state_ == SessionState::dead) break;
    }
    if (transport_.state() == net::ConnectionState::closed && state_ != SessionState::failed &&
        state_ != SessionState::dead) {
      error_ = transport_.error();
      state_ = SessionState::disconnected;
    }
  } catch (const std::exception& e) {
    error_ = e.what();
    state_ = SessionState::failed;
    transport_.close();
  }
}
void Session::finish_frame(std::int32_t time) {
  if (state_ != SessionState::playing) return;
  // Map.update refreshes the party after applying object additions/removals.
  party_.update(world_, time);
  world_.expire_damage(time);
  for (const auto& packet : combat_.update(time)) send_packet(packet);
  world_.particles().update(world_, time);
  if (pending_moves_)
    if (auto* player = world_.entity(world_.player_id())) {
      send(net::outgoing::Move{
          time, {static_cast<float>(player->motion.position.x), static_cast<float>(player->motion.position.y)}});
      player_move_ack(world_, *player, time);
      --pending_moves_;
    }
}
void Session::receive(net::Event event, std::int32_t time) {
  using namespace net;
  // DEATH is terminal: a coalesced reconnect/tick/duplicate cannot replace it.
  if (state_ == SessionState::dead) return;
  switch (event.id) {
    case flash::PacketId::MAPINFO: {
      map_info_ = std::get<MapInfo>(event.data);
      world_.initialize(map_info_.width, map_info_.height, map_info_.display_name);
      combat_.reset(map_info_.seed);
      inventory_.reset();
      party_.reset();
      quest_.reset();
      buy_pending_ = false;
      next_teleport_at_ = 0;
      pending_moves_ = 0;
      ++map_revision_;
      if (options_.create_character)
        send(outgoing::Create{options_.class_type, options_.skin_type});
      else
        send(outgoing::Load{options_.character_id});
      state_ = SessionState::selecting_character;
      break;
    }
    case flash::PacketId::CREATE_SUCCESS: {
      const auto& success = std::get<incoming::CreateSuccess>(event.data);
      world_.set_player_id(success.object_id);
      options_.character_id = success.character_id;
      options_.create_character = false;
      state_ = SessionState::playing;
      break;
    }
    case flash::PacketId::UPDATE:
      world_.apply(std::get<Update>(event.data));
      ++map_revision_;
      break;
    case flash::PacketId::NEWTICK:
      world_.apply(std::get<Tick>(event.data));
      ++pending_moves_;
      break;
    case flash::PacketId::INVRESULT:
      inventory_.result(std::get<incoming::InvResult>(event.data).result);
      break;
    case flash::PacketId::ACCOUNTLIST:
      party_.receive(std::get<incoming::AccountList>(event.data));
      break;
    // onBuyResult clears outstandingBuy_ for every result; the event remains
    // available for the view's chat line or Purchase Error dialog.
    case flash::PacketId::BUYRESULT:
      buy_pending_ = false;
      break;
    case flash::PacketId::NAMERESULT: {
      const auto& result = std::get<incoming::NameResult>(event.data);
      if (result.success && !pending_name_.empty()) options_.username = pending_name_;
      pending_name_.clear();
      break;
    }
    case flash::PacketId::GOTO: {
      const auto& go = std::get<incoming::Goto>(event.data);
      world_.goto_entity(go.object_id, {go.position.x, go.position.y});
      send(outgoing::GotoAck{time});
      break;
    }
    case flash::PacketId::PING:
      send(outgoing::Pong{std::get<incoming::Ping>(event.data).serial, time});
      break;
    case flash::PacketId::FAILURE:
      error_ = std::get<incoming::Failure>(event.data).text;
      state_ = SessionState::failed;
      transport_.close();
      break;
    case flash::PacketId::DEATH:
      death_ = std::get<incoming::Death>(event.data);
      state_ = SessionState::dead;
      pending_moves_ = 0;
      inventory_.reset();
      buy_pending_ = false;
      transport_.close();
      break;
    case flash::PacketId::NOTIFICATION: {
      // GameServerConnection.onNotification: the packet's own text and color for two seconds.
      const auto& note = std::get<incoming::Notification>(event.data);
      // Notifications aimed at other players are dropped when Ally Notifications is off.
      if (!world_.allows_notification(note.object_id)) break;
      world_.notify(note.object_id, note.text, note.color & 0xffffffU, 2000);
      // onNotification: the player's own "Quest Complete!" starts the wait for the next target.
      if (note.object_id == world_.player_id() && world_.entity(note.object_id) && note.text == "Quest Complete!")
        quest_.completed(time);
      break;
    }
    // GameServerConnection.onQuestObjId.
    case flash::PacketId::QUESTOBJID:
      quest_.set_object(std::get<incoming::QuestObject>(event.data).object_id, time);
      break;
    case flash::PacketId::SHOWEFFECT:
      world_.particles().show(world_, std::get<incoming::ShowEffect>(event.data), time);
      break;
    case flash::PacketId::DAMAGE: {
      const auto& damage = std::get<incoming::Damage>(event.data);
      world_.report_damage(damage.target_id, damage.damage, time, damage.effects);
      break;
    }
    // GameServerConnection.onPlaySound: the server's sound name plays at the default volume, through the SFX gate.
    case flash::PacketId::PLAYSOUND:
      world_.sounds().play(std::get<incoming::PlaySound>(event.data).sound);
      break;
    case flash::PacketId::RECONNECT: {
      // MapEditor.onMapTestDone returns to the editor on any reconnect instead of following the portal.
      if (options_.game_id == -4 && !options_.map_json.empty()) {
        disconnect();
        break;
      }
      const auto game_id = std::get<incoming::Reconnect>(event.data).game_id;
      options_.game_id = game_id;
      inventory_.reset();
      transport_.connect(options_.host, options_.port);
      hello_sent_ = false;
      state_ = SessionState::connecting;
      break;
    }
    default:
      break;
  }
  for (const auto& packet : combat_.receive(event, time)) send_packet(packet);
  if (events_.size() >= 1024) events_.erase(events_.begin(), events_.begin() + 512);
  events_.push_back(std::move(event));
}
std::optional<std::string> Session::teleport(std::int32_t object_id, std::int32_t time) {
  if (state_ != SessionState::playing) return {};
  if (!map_info_.allow_teleport) return "Teleporting is not allowed in this map.";
  if (const auto wait = teleport_cooldown(time); wait > 0)
    return "You can not teleport for another " + std::to_string(wait / 1000 + 1) + " seconds.";
  const auto* target = world_.entity(object_id);
  if (!target || !target->player || object_id == world_.player_id()) return {};
  if (target->combat.condition & (1U << 11U))
    return "Can not teleport to " + target->profile.name + " while they are invisible.";
  send(net::outgoing::Teleport{object_id});
  next_teleport_at_ = time + 10000;
  return {};
}
void Session::edit_account_list(std::int32_t object_id, bool ignore, bool add) {
  if (state_ != SessionState::playing) return;
  const auto* target = world_.entity(object_id);
  if (!target) return;
  const auto found = target->profile.stats.find(static_cast<std::uint8_t>(flash::StatId::ACCOUNT_ID_STAT));
  party_.edit(found == target->profile.stats.end() ? -1 : found->second, object_id, ignore, add);
  send(net::outgoing::EditAccountList{ignore ? 1 : 0, add, object_id});
}
}
