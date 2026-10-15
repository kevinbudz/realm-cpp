#include "realm/hud/HudState.hpp"
#include "realm/util/Telemetry.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
HudState HudState::player(const World& world, const Entity& entity, const realm::Party* party,
                          std::int32_t time) {
  using S = flash::StatId;
  const auto* def = world.catalog().object(entity.type);
  const int level = entity.profile.stats.value_or(S::LEVEL_STAT, 1);
  const int hp_cap = def ? def->max_hp_cap : 0;
  const int mp_cap = def ? def->max_mp_cap : 0;
  HudState result{
      entity.profile.name,
      {{{level == 20 ? "Fame" : "Lvl " + std::to_string(level), level == 20 ? 14835456U : 5931045U,
         230, entity.profile.stats.value_or(level == 20 ? S::CHAR_FAME_STAT : S::EXP_STAT, 0),
         entity.profile.stats.value_or(
             level == 20 ? S::NEXT_CLASS_QUEST_FAME_STAT : S::NEXT_LEVEL_EXP_STAT, 0),
         0, -1},
        {"HP", 14693428, 254, entity.combat.hp, entity.combat.max_hp,
         entity.profile.stats.value_or(S::MAX_HP_BOOST_STAT, 0), hp_cap},
        {"MP", 6325472, 278, entity.profile.stats.value_or(S::MP_STAT, 0),
         entity.profile.stats.value_or(S::MAX_MP_STAT, 0),
         entity.profile.stats.value_or(S::MAX_MP_BOOST_STAT, 0), mp_cap}}},
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {}};
  auto& inv = result.inventory;
  inv.enabled = true;
  inv.tiers = world.preferences().show_tier_tag;
  inv.portrait = world.catalog().portrait_type(entity.type, entity.appearance.texture);
  inv.appearance_type = entity.type;
  inv.skin = entity.appearance.texture;
  inv.tex1 = entity.appearance.tex1;
  inv.tex2 = entity.appearance.tex2;
  inv.backpack = entity.profile.stats.value_or(S::HASBACKPACK_STAT, 0) != 0;
  inv.potions = {entity.profile.stats.value_or(S::HEALTH_POTION_STACK_STAT, 0),
                 entity.profile.stats.value_or(S::MAGIC_POTION_STACK_STAT, 0)};
  for (std::size_t i = 0; i < inv.types.size(); ++i) {
    inv.types[i] = def && i < def->slot_types.size() ? def->slot_types[i] : 0;
  }
  const auto* target = world.interactive_target();
  const auto* target_def = target ? world.catalog().object(target->type) : nullptr;
  if (target_def && target_def->container) {
    inv.container = target->id;
  }
  if (target_def && target_def->class_name == "Portal") {
    // PortalPanel.draw: GameObject.getName, ACTIVE_STAT, LockedPortal.
    auto& portal = result.portal;
    portal.id = target->id;
    portal.locked = target_def->xml.has("LockedPortal");
    portal.name = target->profile.name.empty() ? target_def->display_id : target->profile.name;
    if (portal.locked && portal.name.starts_with("Locked ")) {
      portal.name.erase(0, 7);
    }
    const auto active = target->profile.stats.find(static_cast<std::uint8_t>(S::ACTIVE_STAT));
    portal.active = active == target->profile.stats.end() || active->second != 0;
  }
  if (target_def) {
    if (const auto kind = std::find(button_panel_classes.begin(), button_panel_classes.end(),
                                    target_def->class_name);
        kind != button_panel_classes.end()) {
      result.button = {static_cast<int>(kind - button_panel_classes.begin()), target->id, false};
    }
  }
  if (target_def &&
      (target_def->class_name == "Merchant" || target_def->class_name == "ClosedVaultChest" ||
       target_def->class_name == "GuildMerchant")) {
    // soldObjectName/getIcon of each SellableObject class. A Merchant without
    // a merchandise type has no valid name or icon, so no panel is drawn.
    auto& sell = result.sellable;
    const bool guild = target_def->class_name == "GuildMerchant";
    if (target_def->class_name == "Merchant") {
      const int merchandise = target->profile.stats.value_or(S::MERCHANDISE_TYPE_STAT, -1);
      const auto* item = merchandise >= 0 && merchandise <= 65535
                             ? world.catalog().object(static_cast<std::uint16_t>(merchandise))
                             : nullptr;
      if (item) {
        sell.id = target->id;
        sell.name = item->display_id;
        sell.icon = "item" + std::to_string(merchandise);
      }
    } else {
      sell.id = target->id;
      sell.icon = "sellicon" + std::to_string(target_def->type);
      sell.name = guild ? target_def->display_id : "Vault Chest";
    }
    if (sell.id >= 0) {
      sell.price = target->profile.stats.value_or(S::MERCHANDISE_PRICE_STAT,
                                                  guild ? target_def->xml.integer("Price") : 0);
      sell.currency = target->profile.stats.value_or(S::MERCHANDISE_CURRENCY_STAT, guild ? 2 : -1);
      // SellableObjectPanel.draw: rankReq_, then GuildMerchant's guildRankReq_ = LEADER.
      const int rank = target->profile.stats.value_or(S::MERCHANDISE_RANK_REQ_STAT, 0);
      if (entity.profile.stats.value_or(S::NUM_STARS_STAT, 0) < rank) {
        sell.rank = rank;
      } else if (guild && entity.profile.stats.value_or(S::GUILD_RANK_STAT, -1) < 30) {
        sell.guild = true;
      } else {
        sell.button = true;
      }
    }
  }
  if (target_def) {
    auto& panel = result.social;
    using K = SocialKind;
    if (target_def->class_name == "GuildHallPortal") {
      panel = {
          K::hall, target->id, "Guild Hall", entity.profile.guild.empty() ? "Not In Guild" : "",
          "Enter", 0,          -1,           !entity.profile.guild.empty(),
          false};
    } else if (target_def->class_name == "GuildRegister") {
      if (entity.profile.guild.empty()) {
        panel = {
            K::create_guild, target->id, "Create a Guild", "", "Create 1000", 1000, 1, true, false};
      } else {
        const int rank = entity.profile.stats.value_or(S::GUILD_RANK_STAT, -1);
        const std::string rank_name = rank == 0    ? "Initiate"
                                      : rank == 10 ? "Member"
                                      : rank == 20 ? "Officer"
                                      : rank == 30 ? "Leader"
                                      : rank == 40 ? "Founder"
                                                   : "Unknown";
        panel = {K::leave_guild,
                 target->id,
                 rank_name + " of\n" + entity.profile.guild,
                 "",
                 "Renounce",
                 0,
                 -1,
                 true,
                 false};
      }
    } else if (target_def->class_name == "NameChanger") {
      // No caller sets NameChanger.rankRequired in this fork.
      constexpr int rank = 0;
      const bool chosen = entity.profile.stats.value_or(S::NAME_CHOSEN_STAT, 0) != 0;
      const bool allowed = chosen || entity.profile.stats.value_or(S::NUM_STARS_STAT, 0) >= rank;
      panel = {chosen ? K::change_name : K::choose_name,
               target->id,
               chosen ? "Your name is:\n" + entity.profile.name : "Choose Account Name",
               allowed ? "" : "Rank Required: " + std::to_string(rank),
               chosen ? "Change 1000" : "Choose",
               chosen ? 1000 : 0,
               chosen ? 0 : -1,
               allowed,
               false};
    }
  }
  if (party && !target) {
    // InteractPanel falls back to PartyPanel; GameObjectListItem.draw and PartyPanel.draw.
    result.party_members(world, entity, *party, party->members(), time);
  }
  for (std::size_t i = 0; i < inv.items.size(); ++i) {
    if (i >= container_slot && inv.container < 0) {
      break;
    }
    const auto& owner = i < container_slot ? entity : *target;
    const auto slot = i < container_slot ? i : i - container_slot;
    const auto key = static_cast<std::uint8_t>(slot < 12 ? 8 + slot : 61 + slot - 12);
    const auto found = owner.profile.stats.find(key);
    inv.items[i] = found == owner.profile.stats.end() ? -1 : found->second;
    const auto* item = inv.items[i] > 0 && inv.items[i] <= 65535
                           ? world.catalog().object(static_cast<std::uint16_t>(inv.items[i]))
                           : nullptr;
    inv.restricted[i] =
        inv.items[i] != -1 &&
        (!item || (item->slot_type != 10 && std::find(inv.types.begin(), inv.types.end(),
                                                      item->slot_type) == inv.types.end()));
    if (i < 4 && item && item->usable) {
      inv.dim[i] = entity.profile.stats.value_or(S::MP_STAT, 0) <
                   (item->xml.has("MultiPhase") ? item->xml.integer("MpEndCost") : item->mp_cost);
    }
  }
  constexpr std::array values{S::ATTACK_STAT,    S::DEFENSE_STAT,  S::SPEED_STAT,
                              S::DEXTERITY_STAT, S::VITALITY_STAT, S::WISDOM_STAT};
  constexpr std::array boosts{S::ATTACK_BOOST_STAT,   S::DEFENSE_BOOST_STAT,
                              S::SPEED_BOOST_STAT,    S::DEXTERITY_BOOST_STAT,
                              S::VITALITY_BOOST_STAT, S::WISDOM_BOOST_STAT};
  for (std::size_t i = 0; i < values.size(); ++i) {
    result.stats[i] = {entity.profile.stats.value_or(values[i], 0),
                       entity.profile.stats.value_or(boosts[i], 0), def ? def->stat_caps[i] : 0};
  }
  return result;
}

const HudState& HudStateCache::player(const World& world, const Entity& focus, const Party* party,
                                      std::int32_t time) {
  const auto* target = world.interactive_target();
  const auto target_id = target ? target->id : -1;
  const auto party_revision = party ? party->revision() : 0;
  // Public Entity/stat access is used by fixtures and local prediction: a bounded 125 Hz fallback
  // covers those writes too, while packet changes, inventory actions and crossing interaction
  // boundaries refresh immediately.
  if (world_ != &world || generation_ != world.generation() || revision_ != world.hud_revision() ||
      player_id_ != focus.id || target_id_ != target_id || party_ != party ||
      party_revision_ != party_revision || tiers_ != world.preferences().show_tier_tag ||
      time < refreshed_ || time - refreshed_ >= 8) {
    REALM_ZONE("hud.state_refresh");
    state_ = HudState::player(world, focus, party, time);
    world_ = &world;
    generation_ = world.generation();
    revision_ = world.hud_revision();
    player_id_ = focus.id;
    target_id_ = target_id;
    party_ = party;
    party_revision_ = party_revision;
    tiers_ = world.preferences().show_tier_tag;
    refreshed_ = time;
    ++rebuilds_;
  }
  // Low-health party flashes still follow the render clock between structural refreshes.
  for (auto& shown : state_.party.members) {
    if (shown) {
      const auto* member = world.entity(shown->object_id);
      shown->pulse = member && member->combat.hp < member->combat.max_hp * 0.2
                         ? static_cast<int>(std::abs(std::sin(time / 200.0)) * 10)
                         : 0;
    }
  }
  return state_;
}

void HudState::party_members(const World& world, const Entity& player, const realm::Party& source,
                             const std::array<std::int32_t, 6>& ids, std::int32_t time) {
  party.shown = true;
  party.members = {};
  for (std::size_t i = 0; i < ids.size(); ++i) {
    const auto* member = world.entity(ids[i]);
    if (!member || !member->player) {
      continue;
    }
    const auto* definition = world.catalog().object(member->type);
    auto& shown = party.members[i].emplace();
    shown.type = member->type;
    shown.skin = member->appearance.texture;
    shown.tex1 = member->appearance.tex1;
    shown.tex2 = member->appearance.tex2;
    shown.portrait = world.catalog().portrait_type(member->type, member->appearance.texture);
    shown.object_id = member->id;
    shown.name =
        member->profile.name.empty() && definition ? definition->display_id : member->profile.name;
    shown.color = !player.profile.guild.empty() && player.profile.guild == member->profile.guild
                      ? 10944349U
                      : 16572160U;
    shown.dark = !source.starred(member->id);
    if (member->combat.hp < member->combat.max_hp * 0.2) {
      shown.pulse = static_cast<int>(std::abs(std::sin(time / 200.0)) * 10);
    }
  }
}

void HudState::retain(const HudState& previous) {
  const auto& before = previous.sellable;
  auto& now = sellable;
  if (now.id < 0 || before.id != now.id) {
    return;
  }
  // setOwner returns early for the same owner; draw() keeps a contained
  // rankReqText and never removes guildRankReqText.
  now.name = before.name;
  now.icon = before.icon;
  if (now.rank > 0 && before.rank > 0) {
    now.rank = before.rank;
  }
  now.guild = now.guild || before.guild;
}

HudState::PanelButton HudState::panel_button(const FontBook& fonts) const {
  if (portal.id >= 0 && !portal.locked && portal.active) {
    return {portal.id, portal_button};
  }
  if (button.id >= 0) {
    return {button.id, button_panel_buttons.at(static_cast<std::size_t>(button.kind))};
  }
  if (social.id >= 0 && social.enabled) {
    const double width =
        std::ceil(fonts.measure(social.label, 16, true)) + 18 + (social.currency >= 0 ? 24 : 0);
    return {social.id, {100 - width / 2, 562, width, 26}};
  }
  // A disabled LegacyBuyButton is not mouse-enabled.
  if (sellable.id >= 0 && sellable.button && sellable.enabled) {
    return {sellable.id, buy_button_layout(fonts, sellable).area};
  }
  return {};
}

BuyButtonLayout buy_button_layout(const FontBook& fonts, const HudState::Sellable& state) {
  // LegacyBuyButton.updateUI: the autosized price field is int(textWidth + 4)
  // wide; text.x is stored in twips; the icon follows the text width.
  const bool icon = state.currency >= 0 && state.currency <= 2;
  const auto text = std::to_string(state.price);
  const double text_width = fonts.measure(text, 16, true);
  const int icon_width = icon ? 20 : 0;
  const int width = static_cast<int>(std::floor(text_width + 4)) + icon_width + 15;
  const auto twips = std::floor(
      (static_cast<double>(width - icon_width - 5) * 20 - std::round(text_width * 20)) / 2);
  const double x = 100 - width / 2.0;
  const double text_x = twips / 20;
  return {{x, 562, static_cast<double>(width), 26}, x + text_x, x + text_x + text_width + 5, icon};
}
}
