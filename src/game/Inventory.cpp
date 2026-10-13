#include "realm/game/Inventory.hpp"
namespace realm {
namespace {
std::uint8_t item_stat(int slot) { return static_cast<std::uint8_t>(slot < 12 ? 8 + slot : 61 + slot - 12); }
}
InventoryItem inventory_item(const Entity& entity, int slot) {
  if (slot < 0 || slot >= 20) return {};
  return {entity.profile.stats.value_or(item_stat(slot), -1),
          entity.profile.stats.value_or(static_cast<std::uint8_t>(71 + slot), -1)};
}
void set_inventory_item(Entity& entity, int slot, InventoryItem item) {
  if (slot < 0 || slot >= 20) throw std::out_of_range("Inventory slot");
  entity.profile.stats[item_stat(slot)] = item.type;
  entity.profile.stats[static_cast<std::uint8_t>(71 + slot)] = item.data;
}
bool Inventory::valid(const Entity& entity, int slot) const {
  if (slot < 0 || slot >= 20) return false;
  if (entity.player) return slot < (entity.profile.stats.value_or(69, 0) ? 20 : 12);
  const auto* def = world_.catalog().object(entity.type);
  return def && static_cast<std::size_t>(slot) < def->slot_types.size();
}
bool Inventory::accepts(const Entity& entity, int slot, int type) const {
  if (type <= 0) return true;
  const auto* def = world_.catalog().object(entity.type);
  if (!def) return false;
  if (entity.player && slot >= 4) return true;
  if (static_cast<std::size_t>(slot) >= def->slot_types.size()) return false;
  if (!entity.player) return true;
  const auto* item = type <= 65535 ? world_.catalog().object(static_cast<std::uint16_t>(type)) : nullptr;
  return item && item->slot_type == def->slot_types[static_cast<std::size_t>(slot)];
}
std::optional<net::Packet> Inventory::swap(net::Slot first, net::Slot second, std::int32_t time) {
  return exchange(first, second, time, false);
}
// dropWithoutDestTile skips canSwapItems, so OneWayContainer accepts that drop.
std::optional<net::Packet> Inventory::exchange(net::Slot first, net::Slot second, std::int32_t time, bool one_way) {
  auto* a = world_.entity(first.object_id);
  auto* b = world_.entity(second.object_id);
  const auto* player = world_.entity(world_.player_id());
  if (!player || !a || !b || !valid(*a, first.slot_id) || !valid(*b, second.slot_id) ||
      (first.object_id == second.object_id && first.slot_id == second.slot_id))
    return {};
  const auto* target = world_.catalog().object(b->type);
  if (!one_way && target && target->class_name == "OneWayContainer") return {};
  const auto av = inventory_item(*a, first.slot_id), bv = inventory_item(*b, second.slot_id);
  if (av.type == -1 || !accepts(*a, first.slot_id, bv.type) || !accepts(*b, second.slot_id, av.type)) return {};
  pending_.push_back({{first, av}, {second, bv}, true});
  const auto packet = net::outgoing::encode(net::outgoing::InvSwap{
      time,
      {static_cast<float>(player->motion.position.x), static_cast<float>(player->motion.position.y)},
      first,
      second});
  world_.invalidate_hud();
  set_inventory_item(*a, first.slot_id, bv);
  set_inventory_item(*b, second.slot_id, av);
  // GameServerConnection.invSwap plays inventory_move_item once the swap is sent.
  world_.sounds().play("inventory_move_item");
  return packet;
}
std::optional<net::Packet> Inventory::drop(int slot) {
  auto* player = world_.entity(world_.player_id());
  if (!player || !valid(*player, slot)) return {};
  const auto item = inventory_item(*player, slot);
  if (item.type == -1) return {};
  const net::Slot key{player->id, static_cast<std::uint8_t>(slot)};
  pending_.push_back({{key, item}, {}, false});
  world_.invalidate_hud();
  set_inventory_item(*player, slot, {});
  return net::outgoing::encode(net::outgoing::InvDrop{key.slot_id});
}
std::optional<net::Packet> Inventory::drop_to_ground(int slot, std::int32_t time) {
  const auto* player = world_.entity(world_.player_id());
  const auto* target = world_.interactive_target();
  const auto* def = target ? world_.catalog().object(target->type) : nullptr;
  if (player && def && def->container)
    for (std::size_t i = 0; i < def->slot_types.size() && i < 20; ++i)
      if (inventory_item(*target, static_cast<int>(i)).type < 0)
        return exchange({player->id, static_cast<std::uint8_t>(slot)}, {target->id, static_cast<std::uint8_t>(i)}, time,
                        true);
  return drop(slot);
}
std::optional<net::Packet> Inventory::use(net::Slot slot, std::int32_t time) {
  // GameServerConnection.useItem_new: a usable item plays use_potion, anything else plays error.
  auto* owner = world_.entity(slot.object_id);
  if (!owner || !valid(*owner, slot.slot_id)) {
    world_.sounds().play("error");
    return {};
  }
  const auto item = inventory_item(*owner, slot.slot_id);
  const auto* def =
      item.type > 0 && item.type <= 65535 ? world_.catalog().object(static_cast<std::uint16_t>(item.type)) : nullptr;
  if (!def || (!def->consumable && !def->inventory_use)) {
    world_.sounds().play("error");
    return {};
  }
  pending_.push_back({{slot, item}, {}, false});
  world_.invalidate_hud();
  if (def->consumable) set_inventory_item(*owner, slot.slot_id, {});
  world_.sounds().play("use_potion");
  return net::outgoing::encode(net::outgoing::UseItem{time, slot, {0, 0}});
}
std::optional<net::Packet> Inventory::equip_or_use(net::Slot slot, std::int32_t time) {
  const auto* player = world_.entity(world_.player_id());
  const auto* owner = world_.entity(slot.object_id);
  if (!player || !owner || !valid(*owner, slot.slot_id)) return {};
  if (!owner->player) {
    const int end = player->profile.stats.value_or(69, 0) ? 20 : 12;
    for (int i = 4; i < end; ++i)
      if (inventory_item(*player, i).type <= 0) return swap(slot, {player->id, static_cast<std::uint8_t>(i)}, time);
    return use(slot, time);
  }
  const auto value = inventory_item(*owner, slot.slot_id);
  const auto* item =
      value.type > 0 && value.type <= 65535 ? world_.catalog().object(static_cast<std::uint16_t>(value.type)) : nullptr;
  const auto* def = world_.catalog().object(player->type);
  if (item && def)
    for (std::size_t i = 0; i < 4 && i < def->slot_types.size(); ++i)
      if (def->slot_types[i] == item->slot_type) return swap(slot, {player->id, static_cast<std::uint8_t>(i)}, time);
  return use(slot, time);
}
std::optional<net::Packet> Inventory::use_potion(int position, std::int32_t time) {
  auto* player = world_.entity(world_.player_id());
  if (!player || position < 0 || position > 1) return {};
  const auto id = static_cast<std::uint8_t>(position ? 60 : 59);
  const int count = player->profile.stats.value_or(id, 0);
  if (count <= 0 || (position ? player->profile.stats.value_or(4, 0) >= player->profile.stats.value_or(3, 0)
                              : player->combat.hp >= player->combat.max_hp))
    return {};
  const net::Slot slot{player->id, static_cast<std::uint8_t>(254 + position)};
  pending_.push_back(
      {{slot, {}}, {}, false, true, {player->profile.stats.value_or(59, 0), player->profile.stats.value_or(60, 0)}});
  player->profile.stats[id] = count - 1;
  world_.invalidate_hud();
  // UsePotionCommand.sendServerRequest: the sound follows the request, not the server's answer.
  world_.sounds().play("use_potion");
  return net::outgoing::encode(net::outgoing::UseItem{
      time, slot, {static_cast<float>(player->motion.position.x), static_cast<float>(player->motion.position.y)}});
}
void Inventory::track_ability(const net::Packet& packet) {
  if (packet.id != static_cast<std::uint8_t>(flash::PacketId::USEITEM)) return;
  net::Reader reader(packet.body);
  (void)reader.i32();
  const net::Slot slot{reader.i32(), reader.u8()};
  if (const auto* owner = world_.entity(slot.object_id); owner && valid(*owner, slot.slot_id))
    pending_.push_back({{slot, inventory_item(*owner, slot.slot_id)}, {}, false});
}
void Inventory::restore(const Saved& saved) {
  // Player vectors retain all twenty slots even while the backpack is hidden.
  if (auto* entity = world_.entity(saved.slot.object_id);
      entity && (entity->player ? saved.slot.slot_id < 20 : valid(*entity, saved.slot.slot_id)))
    set_inventory_item(*entity, saved.slot.slot_id, saved.item);
}
void Inventory::result(int code) {
  // GameServerConnection.onInvResult: any failure plays error, even when no operation was pending.
  if (code != 0) world_.sounds().play("error");
  if (pending_.empty()) return;
  const auto op = pending_.front();
  pending_.pop_front();
  if (code == 0) return;
  world_.invalidate_hud();
  restore(op.first);
  if (op.paired) restore(op.second);
  if (auto* player = world_.entity(world_.player_id()); op.potion && player) {
    player->profile.stats[59] = op.stacks[0];
    player->profile.stats[60] = op.stacks[1];
  }
}
}
