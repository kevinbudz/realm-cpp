#include "realm/session.hpp"
#include "realm/movement.hpp"
namespace realm {
void Session::connect(SessionOptions options) {
    transport_.close(); options_ = std::move(options); hello_sent_ = false; pending_moves_ = 0; error_.clear(); events_.clear();
    inventory_.reset();pending_name_.clear();buy_pending_=false;
    transport_.connect(options_.host, options_.port); state_ = SessionState::connecting;
}
void Session::disconnect() { transport_.close();inventory_.reset();pending_name_.clear();buy_pending_=false; state_ = SessionState::disconnected; }
void Session::poll(std::int32_t time) {
    if (state_ == SessionState::disconnected || state_ == SessionState::failed) return;
    try {
        auto packets = transport_.poll();
        if (transport_.state() == net::ConnectionState::connected && !hello_sent_) {
            send(net::outgoing::Hello{options_.build_version, options_.game_id, options_.username, options_.password, options_.map_json});
            hello_sent_ = true;
        }
        for (const auto& packet : packets) receive(net::decode(packet), time);
        if (transport_.state() == net::ConnectionState::closed && state_ != SessionState::failed) { error_ = transport_.error(); state_ = SessionState::disconnected; }
    } catch (const std::exception& e) { error_ = e.what(); state_ = SessionState::failed; transport_.close(); }
}
void Session::finish_frame(std::int32_t time) {
    if (state_ != SessionState::playing) return;
    // Map.update refreshes the party after applying object additions/removals.
    party_.update(world_,time);
    world_.expire_damage(time);
    for (const auto& packet : combat_.update(time)) transport_.send(packet);
    if (pending_moves_) if (auto* player = world_.entity(world_.player_id())) {
        send(net::outgoing::Move{time,{static_cast<float>(player->position.x),static_cast<float>(player->position.y)}});
        player_move_ack(world_,*player,time); --pending_moves_;
    }
}
void Session::receive(net::Event event, std::int32_t time) {
    using namespace net;
    switch (event.id) {
    case flash::PacketId::MAPINFO: {
        map_info_ = std::get<MapInfo>(event.data);
        world_.initialize(map_info_.width, map_info_.height, map_info_.display_name); combat_.reset(map_info_.seed);inventory_.reset();party_.reset();buy_pending_=false; pending_moves_=0; ++map_revision_;
        if (options_.create_character) send(outgoing::Create{options_.class_type, options_.skin_type});
        else send(outgoing::Load{options_.character_id});
        state_ = SessionState::selecting_character; break;
    }
    case flash::PacketId::CREATE_SUCCESS: {
        const auto& success = std::get<incoming::CreateSuccess>(event.data);
        world_.set_player_id(success.object_id); options_.character_id = success.character_id; options_.create_character = false;
        state_ = SessionState::playing; break;
    }
    case flash::PacketId::UPDATE: world_.apply(std::get<Update>(event.data)); ++map_revision_; break;
    case flash::PacketId::NEWTICK: world_.apply(std::get<Tick>(event.data)); ++pending_moves_; break;
    case flash::PacketId::INVRESULT: inventory_.result(std::get<incoming::InvResult>(event.data).result);break;
    case flash::PacketId::ACCOUNTLIST: party_.receive(std::get<incoming::AccountList>(event.data));break;
    // onBuyResult clears outstandingBuy_ for every result; its chat line and
    // "Purchase Error" dialog await those views.
    case flash::PacketId::BUYRESULT: buy_pending_=false;break;
    case flash::PacketId::NAMERESULT: {
        const auto& result=std::get<incoming::NameResult>(event.data);
        if(result.success&&!pending_name_.empty())options_.username=pending_name_;
        pending_name_.clear();break;
    }
    case flash::PacketId::GOTO: {
        const auto& go = std::get<incoming::Goto>(event.data); world_.goto_entity(go.object_id, {go.position.x,go.position.y}); send(outgoing::GotoAck{time}); break;
    }
    case flash::PacketId::PING: send(outgoing::Pong{std::get<incoming::Ping>(event.data).serial,time}); break;
    case flash::PacketId::FAILURE: error_ = std::get<incoming::Failure>(event.data).text; state_ = SessionState::failed; transport_.close(); break;
    case flash::PacketId::DEATH: state_ = SessionState::dead; break;
    case flash::PacketId::DAMAGE: {
        const auto& damage = std::get<incoming::Damage>(event.data);
        world_.report_damage(damage.target_id,damage.damage,time,damage.effects);
        break;
    }
    case flash::PacketId::RECONNECT: {
        const auto game_id = std::get<incoming::Reconnect>(event.data).game_id;
        options_.game_id = game_id;inventory_.reset(); transport_.connect(options_.host, options_.port); hello_sent_ = false; state_ = SessionState::connecting; break;
    }
    default: break;
    }
    for (const auto& packet : combat_.receive(event,time)) transport_.send(packet);
    if (events_.size() >= 1024) events_.erase(events_.begin(), events_.begin() + 512);
    events_.push_back(std::move(event));
}
}
