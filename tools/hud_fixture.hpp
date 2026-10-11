#pragma once
#include "realm/hud.hpp"
#include <nlohmann/json.hpp>
inline realm::HudState hud_fixture(const nlohmann::json& data,const realm::Catalog& catalog) {
    realm::HudState state;
    if(data.contains("bars")) {
        state.name=data.at("name");std::size_t index{};
        for(const auto& bar:data.at("bars"))state.bars.at(index++)={bar.at("label"),bar.at("color"),bar.at("y"),bar.at("value"),bar.at("maximum"),bar.at("boost"),bar.at("cap")};
    } else {
        realm::World world(catalog);world.initialize(1,1);world.add(1,data.at("type"),{0,0});world.set_player_id(1);
        auto& entity=*world.entity(1);entity.stats[4]=data.at("mp");entity.stats[69]=data.at("backpack").get<bool>();
        std::size_t i{};for(const auto& item:data.at("items")) { entity.stats[static_cast<std::uint8_t>(i<12?8+i:61+i-12)]=item;++i; }
        if(data.contains("stats")) {
            constexpr std::array<std::uint8_t,6> values{20,21,22,25,23,24},boosts{45,46,47,50,48,49};
            i=0;for(const auto& stat:data.at("stats")) { entity.stats[values.at(i)]=stat.at("value");entity.stats[boosts.at(i)]=stat.at("boost");++i; }
        }
        if(data.contains("potions")) { entity.stats[59]=data.at("potions").at(0);entity.stats[60]=data.at("potions").at(1); }
        if(data.contains("container")) {
            const auto& container=data.at("container");auto& bag=world.add(2,container.at("type"),{0.5F,0});i=0;
            for(const auto& item:container.at("items"))bag.stats[static_cast<std::uint8_t>(8+i++)]=item;
        }
        if(data.contains("portal")) {
            const auto& portal=data.at("portal");auto& gate=world.add(3,portal.at("type"),{0,0.5F});
            gate.name=portal.at("name");gate.stats[34]=portal.at("active").get<bool>();
        }
        if(data.contains("button"))world.add(4,data.at("button").at("type"),{0,0.5F});
        if(data.contains("sellable")) {
            const auto& fixture=data.at("sellable");auto& owner=world.add(5,fixture.at("type"),{0.5F,0.5F});
            if(fixture.contains("merchandise"))owner.stats[31]=fixture.at("merchandise");
            if(fixture.contains("price"))owner.stats[32]=fixture.at("price");
            if(fixture.contains("currency"))owner.stats[37]=fixture.at("currency");
            if(fixture.contains("rankReq"))owner.stats[42]=fixture.at("rankReq");
            entity.stats[27]=fixture.at("stars");entity.stats[57]=fixture.at("guildRank");
        }
        realm::Party party;std::int32_t time{};
        if(data.contains("party")) {
            const auto& fixture=data.at("party");time=fixture.at("time");world.entity(1)->guild=fixture.at("guild");
            for(const auto& other:fixture.at("players")) {
                auto& member=world.add(other.at("id"),other.at("type"),{other.at("x"),other.at("y")});
                member.name=other.at("name");member.guild=other.at("guild");member.hp=other.at("hp");member.max_hp=other.at("maxHp");
                if(other.contains("account"))member.stats[35]=other.at("account");
            }
            party.receive({0,fixture.at("stars").get<std::vector<std::int32_t>>()});party.update(world,time);
        }
        state=realm::HudState::player(world,*world.entity(1),data.contains("party") ? &party : nullptr,time);
        if(data.contains("portal"))state.portal.hover=data.at("portal").at("hover");
        state.inventory.selected=data.at("selected");state.inventory.tiers=data.at("tiers");
        if(!data.contains("potions"))state.inventory.potions={-1,-1};
        if(data.contains("button"))state.button.hover=data.at("button").at("hover");
        if(data.contains("sellable")) {
            state.sellable.enabled=!data.at("sellable").at("outstanding").get<bool>();state.sellable.hover=data.at("sellable").at("hover");
        }
    }
    return state;
}
