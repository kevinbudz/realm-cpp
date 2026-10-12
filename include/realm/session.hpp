#pragma once
#include "realm/messages.hpp"
#include "realm/http.hpp"
#include "realm/transport.hpp"
#include "realm/world.hpp"
#include "realm/combat.hpp"
#include "realm/inventory.hpp"
#include "realm/party.hpp"
#include <functional>
namespace realm {
enum class SessionState { disconnected, connecting, selecting_character, playing, dead, failed };
struct SessionOptions {
    std::string host{"127.0.0.1"}, build_version{"1.0.0"}, username, password, map_json;
    std::uint16_t port{2050}, class_type{0x030e}, skin_type{};
    std::int32_t game_id{-1}, character_id{};
    bool create_character{};
};
class Session {
public:
    [[nodiscard]] std::size_t outgoing_queue_bytes() const { return transport_.queued_bytes(); }
    [[nodiscard]] std::size_t framing_queue_bytes() const { return transport_.pending_bytes(); }
    [[nodiscard]] std::size_t projectile_count() const { return combat_.projectiles().size(); }
    using PacketSink=std::function<void(const net::Packet&)>;
    // A supplied sink selects deterministic offline replay; connect() is forbidden.
    // Live sessions keep the real transport and unchanged receive/finish behavior.
    explicit Session(World& world) : world_(world), combat_(world),inventory_(world) {}
    Session(World& world,PacketSink sink,SessionOptions options) : replay_sink_(std::move(sink)),world_(world),combat_(world),inventory_(world),options_(std::move(options)) { if(!replay_sink_)throw std::invalid_argument("Replay requires a packet sink"); }
    void replay(const net::Packet& packet,std::int32_t time_ms);
    [[nodiscard]] const std::vector<FlyingProjectile>& projectiles() const { return combat_.projectiles(); }
    [[nodiscard]] const TargetStats& target_stats() const { return combat_.target_stats(); }
    void connect(SessionOptions options);
    void disconnect();
    // The game ID of the current connection (Parameters.NEXUS_GAMEID is -1).
    [[nodiscard]] std::int32_t game_id() const { return options_.game_id; }
    void poll(std::int32_t time_ms);
    void finish_frame(std::int32_t time_ms);
    void shoot(double angle, std::int32_t time_ms) { if (state_==SessionState::playing) for (const auto& packet : combat_.shoot(angle,time_ms)) send_packet(packet); }
    void use_ability(Vec2 target, std::int32_t time_ms) { if(state_==SessionState::playing) for(const auto& packet:combat_.use_ability(target,time_ms)) { inventory_.track_ability(packet);send_packet(packet); } }
    void swap_items(net::Slot first,net::Slot second,std::int32_t time) { if(state_==SessionState::playing)if(const auto packet=inventory_.swap(first,second,time))send_packet(*packet); }
    void drop_item(int slot) { if(state_==SessionState::playing)if(const auto packet=inventory_.drop(slot))send_packet(*packet); }
    void drop_to_ground(int slot,std::int32_t time) { if(state_==SessionState::playing)if(const auto packet=inventory_.drop_to_ground(slot,time))send_packet(*packet); }
    void use_item(net::Slot slot,std::int32_t time) { if(state_==SessionState::playing)if(const auto packet=inventory_.use(slot,time))send_packet(*packet); }
    void use_potion(int position,std::int32_t time) { if(state_==SessionState::playing)if(const auto packet=inventory_.use_potion(position,time))send_packet(*packet); }
    void equip_or_use(net::Slot slot,std::int32_t time) { if(state_==SessionState::playing)if(const auto packet=inventory_.equip_or_use(slot,time))send_packet(*packet); }
    void use_portal(std::int32_t object_id) { if(state_==SessionState::playing)send(net::outgoing::UsePortal{object_id}); }
    // GameServerConnection.buy: one outstanding purchase, for an object still in the map.
    void buy(std::int32_t object_id) { if(state_==SessionState::playing&&!buy_pending_&&world_.entity(object_id)) { buy_pending_=true;send(net::outgoing::Buy{object_id}); } }
    [[nodiscard]] bool buy_pending() const { return buy_pending_; }
    bool reskin(int skin_id,std::span<const int> owned);
    void choose_name(const std::string& name) { if(state_==SessionState::playing&&pending_name_.empty()) { pending_name_=name;send(net::outgoing::ChooseName{name}); } }
    void create_guild(const std::string& name) { if(state_==SessionState::playing)send(net::outgoing::CreateGuild{name}); }
    void leave_guild(const std::string& name) { if(state_==SessionState::playing)send(net::outgoing::GuildRemove{name}); }
    // Player.teleportTo: one TELEPORT per MS_BETWEEN_TELEPORT, never to an
    // invisible player or on a map that disables teleport. Returns a refusal, if any.
    [[nodiscard]] std::optional<std::string> teleport(std::int32_t object_id,std::int32_t time_ms);
    [[nodiscard]] std::int32_t teleport_cooldown(std::int32_t time_ms) const { return std::max<std::int32_t>(0,next_teleport_at_-time_ms); }
    // TextBox Enter: PLAYERTEXT; the server interprets slash commands.
    void player_text(const std::string& text) { if(state_==SessionState::playing)send(net::outgoing::PlayerText{text}); }
    void request_trade(const std::string& name) { if(state_==SessionState::playing)send(net::outgoing::RequestTrade{name}); }
    void change_trade(const std::vector<bool>& offer) { if(state_==SessionState::playing)send(net::outgoing::ChangeTrade{offer}); }
    void accept_trade(const std::vector<bool>& mine,const std::vector<bool>& theirs) { if(state_==SessionState::playing)send(net::outgoing::AcceptTrade{mine,theirs}); }
    void cancel_trade() { if(state_==SessionState::playing)send(net::outgoing::CancelTrade{}); }
    void change_guild_rank(const std::string& name,int rank) { if(state_==SessionState::playing)send(net::outgoing::ChangeGuildRank{name,rank}); }
    void guild_invite(const std::string& name) { if(state_==SessionState::playing)send(net::outgoing::GuildInvite{name}); }
    // Party.lockPlayer/unlockPlayer/ignorePlayer/unignorePlayer: EDITACCOUNTLIST
    // list 0 stars, list 1 ignores.
    void edit_account_list(std::int32_t object_id,bool ignore,bool add);
    void escape() { if(state_==SessionState::playing && map_info_.name!="Nexus")send(net::outgoing::Escape{}); }
    template<class T> void send(const T& message) { send_packet(net::outgoing::encode(message)); }
    [[nodiscard]] SessionState state() const { return state_; }
    [[nodiscard]] const std::string& error() const { return error_; }
    [[nodiscard]] std::vector<net::Event> take_events() { auto result = std::move(events_); events_.clear(); return result; }
    [[nodiscard]] std::uint64_t map_revision() const { return map_revision_; }
    [[nodiscard]] const net::MapInfo& map_info() const { return map_info_; }
    [[nodiscard]] std::int32_t character_id() const { return options_.character_id; }
    [[nodiscard]] net::FormFields credentials() const { return {{"username",options_.username},{"password",options_.password}}; }
    [[nodiscard]] const Party& party() const { return party_; }
private:
    void send_packet(const net::Packet& packet);
    PacketSink replay_sink_;
    void receive(net::Event event, std::int32_t time_ms);
    World& world_;
    net::Transport transport_;
    Combat combat_;
    Inventory inventory_;
    Party party_;
    // GameServerConnection.outstandingBuy_.
    bool buy_pending_{};
    std::int32_t next_teleport_at_{};
    SessionOptions options_;
    SessionState state_{SessionState::disconnected};
    bool hello_sent_{};
    std::size_t pending_moves_{};
    std::string error_,pending_name_;
    std::uint64_t map_revision_{};
    net::MapInfo map_info_;
    std::vector<net::Event> events_;
};
}
