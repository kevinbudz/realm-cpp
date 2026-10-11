#pragma once
#include "realm/messages.hpp"
#include "realm/transport.hpp"
#include "realm/world.hpp"
#include "realm/combat.hpp"
#include "realm/inventory.hpp"
#include "realm/party.hpp"
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
    explicit Session(World& world) : world_(world), combat_(world),inventory_(world) {}
    void connect(SessionOptions options);
    void disconnect();
    void poll(std::int32_t time_ms);
    void finish_frame(std::int32_t time_ms);
    void shoot(double angle, std::int32_t time_ms) { if (state_==SessionState::playing) for (const auto& packet : combat_.shoot(angle,time_ms)) transport_.send(packet); }
    void use_ability(Vec2 target, std::int32_t time_ms) { if(state_==SessionState::playing) for(const auto& packet:combat_.use_ability(target,time_ms)) { inventory_.track_ability(packet);transport_.send(packet); } }
    void swap_items(net::Slot first,net::Slot second,std::int32_t time) { if(state_==SessionState::playing)if(const auto packet=inventory_.swap(first,second,time))transport_.send(*packet); }
    void drop_item(int slot) { if(state_==SessionState::playing)if(const auto packet=inventory_.drop(slot))transport_.send(*packet); }
    void drop_to_ground(int slot,std::int32_t time) { if(state_==SessionState::playing)if(const auto packet=inventory_.drop_to_ground(slot,time))transport_.send(*packet); }
    void use_item(net::Slot slot,std::int32_t time) { if(state_==SessionState::playing)if(const auto packet=inventory_.use(slot,time))transport_.send(*packet); }
    void use_potion(int position,std::int32_t time) { if(state_==SessionState::playing)if(const auto packet=inventory_.use_potion(position,time))transport_.send(*packet); }
    void equip_or_use(net::Slot slot,std::int32_t time) { if(state_==SessionState::playing)if(const auto packet=inventory_.equip_or_use(slot,time))transport_.send(*packet); }
    void use_portal(std::int32_t object_id) { if(state_==SessionState::playing)send(net::outgoing::UsePortal{object_id}); }
    // GameServerConnection.buy: one outstanding purchase, for an object still in the map.
    void buy(std::int32_t object_id) { if(state_==SessionState::playing&&!buy_pending_&&world_.entity(object_id)) { buy_pending_=true;send(net::outgoing::Buy{object_id}); } }
    [[nodiscard]] bool buy_pending() const { return buy_pending_; }
    void choose_name(const std::string& name) { if(state_==SessionState::playing&&pending_name_.empty()) { pending_name_=name;send(net::outgoing::ChooseName{name}); } }
    void create_guild(const std::string& name) { if(state_==SessionState::playing)send(net::outgoing::CreateGuild{name}); }
    void leave_guild(const std::string& name) { if(state_==SessionState::playing)send(net::outgoing::GuildRemove{name}); }
    void escape() { if(state_==SessionState::playing && map_info_.name!="Nexus")send(net::outgoing::Escape{}); }
    template<class T> void send(const T& message) { transport_.send(net::outgoing::encode(message)); }
    [[nodiscard]] SessionState state() const { return state_; }
    [[nodiscard]] const std::string& error() const { return error_; }
    [[nodiscard]] std::vector<net::Event> take_events() { auto result = std::move(events_); events_.clear(); return result; }
    [[nodiscard]] std::uint64_t map_revision() const { return map_revision_; }
    [[nodiscard]] const net::MapInfo& map_info() const { return map_info_; }
    [[nodiscard]] std::int32_t character_id() const { return options_.character_id; }
    [[nodiscard]] const Party& party() const { return party_; }
private:
    void receive(net::Event event, std::int32_t time_ms);
    World& world_;
    net::Transport transport_;
    Combat combat_;
    Inventory inventory_;
    Party party_;
    // GameServerConnection.outstandingBuy_.
    bool buy_pending_{};
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
