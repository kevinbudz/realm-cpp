#pragma once
#include "realm/messages.hpp"
#include "realm/world.hpp"
#include <array>
#include <deque>
#include <optional>
namespace realm {
struct InventoryItem { int type{-1},data{-1}; bool operator==(const InventoryItem&) const = default; };
[[nodiscard]] InventoryItem inventory_item(const Entity& entity,int slot);
void set_inventory_item(Entity& entity,int slot,InventoryItem item);
// This fork acknowledges every inventory operation in send order. Prediction
// changes only slots; HP, MP, conditions and equipment bonuses remain server-owned.
class Inventory {
public:
    explicit Inventory(World& world):world_(world) {}
    [[nodiscard]] std::optional<net::Packet> swap(net::Slot first,net::Slot second,std::int32_t time);
    [[nodiscard]] std::optional<net::Packet> drop(int slot);
    // ItemGridMediator.dropItem: a player item dropped on the world fills the
    // first empty slot of a nearby container, otherwise it becomes INVDROP.
    [[nodiscard]] std::optional<net::Packet> drop_to_ground(int slot,std::int32_t time);
    [[nodiscard]] std::optional<net::Packet> use(net::Slot slot,std::int32_t time);
    [[nodiscard]] std::optional<net::Packet> equip_or_use(net::Slot slot,std::int32_t time);
    // UsePotionCommand: position 0 is health (slot 254), 1 is magic (slot 255).
    // Nothing is sent without stock or while that meter is full.
    [[nodiscard]] std::optional<net::Packet> use_potion(int position,std::int32_t time);
    // Ability USEITEM also receives INVRESULT and must occupy its FIFO position.
    void track_ability(const net::Packet& packet);
    void result(int code);
    void reset() { pending_.clear(); }
    [[nodiscard]] std::size_t pending() const { return pending_.size(); }
private:
    struct Saved { net::Slot slot;InventoryItem item; };
    // A rejected potion use restores both stack counts captured before it was sent.
    struct Operation { Saved first,second;bool paired{},potion{};std::array<int,2> stacks{}; };
    [[nodiscard]] std::optional<net::Packet> exchange(net::Slot first,net::Slot second,std::int32_t time,bool one_way);
    [[nodiscard]] bool valid(const Entity& entity,int slot) const;
    [[nodiscard]] bool accepts(const Entity& entity,int slot,int type) const;
    void restore(const Saved& saved);
    World& world_;
    std::deque<Operation> pending_;
};
}
