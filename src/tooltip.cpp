#include "realm/tooltip.hpp"
#include "realm/item_data.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace realm {
namespace {
constexpr std::array<int,8> stat_ids{0,3,20,21,22,28,26,27};
// XML stat IDs and this fork's wire stat IDs differ for DEX/VIT/WIS.
constexpr std::array<int,8> wire_stat_ids{0,3,20,21,22,25,23,24};
constexpr std::array<std::string_view,8> stat_names{"Max HP","Max MP","ATT","DEF","SPD","DEX","VIT","WIS"};
constexpr std::uint32_t effect_color=0xffff8f,warning_color=0xff7777;
int stat(const Entity& entity,int id,int fallback=0) {
    const auto found=entity.stats.find(static_cast<std::uint8_t>(id));return found==entity.stats.end() ? fallback : found->second;
}
std::string number(double value) {
    std::ostringstream out;out<<std::fixed<<std::setprecision(2)<<value;
    auto text=out.str();while(text.size()>1&&text.back()=='0')text.pop_back();if(text.back()=='.')text.pop_back();return text;
}
std::string signed_number(double value) { return (value>0 ? "+" : "")+number(value); }
int stat_index(int id) {
    const auto found=std::find(stat_ids.begin(),stat_ids.end(),id);return found==stat_ids.end() ? -1 : static_cast<int>(found-stat_ids.begin());
}
std::string stat_name(int id) { const int i=stat_index(id);return i<0 ? "stat "+std::to_string(id) : std::string(stat_names[static_cast<std::size_t>(i)]); }
std::array<int,8> bonuses(const ObjectDef& item,int data) {
    std::array<int,8> values{};
    for(const auto activate:item.xml.children("ActivateOnEquip"))if(activate.text()=="IncrementStat") {
        const int i=stat_index(std::stoi(activate.attribute("stat","-1")));
        if(i>=0)values[static_cast<std::size_t>(i)]+=std::stoi(activate.attribute("amount","0"));
    }
    for(std::size_t i=0;i<values.size();++i)values[i]+=static_cast<int>(item_data::stat(data,1U<<(8U+static_cast<unsigned>(i)),i<2 ? 5 : 1));
    return values;
}
const ObjectDef* item_definition(const Catalog& catalog,int type) { return type>0&&type<=65535 ? catalog.object(static_cast<std::uint16_t>(type)) : nullptr; }
// Font bitmaps contain straight alpha; HUD art contains premultiplied pixels.
void blit(Image& destination,const Image& source,int x,int y,bool premultiplied=false) {
    for(int row=std::max(0,-y);row<std::min(source.height,destination.height-y);++row)
        for(int col=std::max(0,-x);col<std::min(source.width,destination.width-x);++col) {
            const auto src=static_cast<std::size_t>(row*source.width+col)*4;
            const auto dst=static_cast<std::size_t>((row+y)*destination.width+col+x)*4;
            const unsigned alpha=source.pixels[src+3];if(!alpha)continue;
            for(unsigned c=0;c<3;++c) {
                const unsigned value=premultiplied ? source.pixels[src+c]*255U/alpha : source.pixels[src+c];
                destination.pixels[dst+c]=static_cast<unsigned char>((std::min(value,255U)*alpha+destination.pixels[dst+c]*(255U-alpha)+127U)/255U);
            }
            destination.pixels[dst+3]=255;
        }
}
void fill(Image& image,std::uint32_t color) {
    for(std::size_t i=0;i<image.pixels.size();i+=4) {
        image.pixels[i]=static_cast<unsigned char>(color>>16U);image.pixels[i+1]=static_cast<unsigned char>(color>>8U);
        image.pixels[i+2]=static_cast<unsigned char>(color);image.pixels[i+3]=255;
    }
}
}
std::optional<ItemTooltipTarget> item_tooltip_target(const World& world,const HudState& hud,Vec2 pointer) {
    const auto* player=world.entity(world.player_id());if(!player||!hud.inventory.enabled||hud.inventory.dragging>=0)return {};
    ItemTooltipTarget result;result.player_type=player->type;result.backpack=hud.inventory.backpack;
    const auto* class_def=world.catalog().object(player->type);
    const int potion=HudInput::potion_at(pointer,hud.inventory.selected);
    const int slot=HudInput::slot_at(pointer,hud.inventory.selected,hud.inventory.container>=0);
    if(potion>=0) {
        result.item={potion_types[static_cast<std::size_t>(potion)],-1};result.owner=TooltipOwner::potion;
    } else if(slot>=0) {
        result.slot=slot;result.owner=slot>=container_slot ? TooltipOwner::container : TooltipOwner::player;
        const auto* owner=slot>=container_slot ? world.entity(hud.inventory.container) : player;
        if(!owner)return {};
        result.item=inventory_item(*owner,slot>=container_slot ? slot-container_slot : slot);
    } else if(hud.sellable.id>=0&&pointer.x>=0&&pointer.x<50&&pointer.y>=500&&pointer.y<550) {
        const auto* merchant=world.entity(hud.sellable.id);if(!merchant)return {};
        const auto* definition=world.catalog().object(merchant->type);if(!definition||definition->class_name!="Merchant")return {};
        result.item={stat(*merchant,static_cast<int>(flash::StatId::MERCHANDISE_TYPE_STAT),-1),-1};result.owner=TooltipOwner::merchant;
    } else return {};
    const auto* item=item_definition(world.catalog(),result.item.type);if(!item||item->slot_type<0)return {};
    result.usable=item->slot_type==10;
    if(class_def)for(std::size_t i=0;i<std::min<std::size_t>(4,class_def->slot_types.size());++i)if(class_def->slot_types[i]==item->slot_type) {
        result.usable=true;
        if(result.owner!=TooltipOwner::player||result.slot!=static_cast<int>(i))result.equipped=inventory_item(*player,static_cast<int>(i));
        break;
    }
    for(std::size_t i=0;i<wire_stat_ids.size();++i)result.stats[i]=stat(*player,wire_stat_ids[i]);
    return result;
}
ItemTooltipContent item_tooltip_content(const Catalog& catalog,const ItemTooltipTarget& target) {
    ItemTooltipContent content;const auto* item=item_definition(catalog,target.item.type);if(!item)return content;
    const auto modifier_color=item_data::color(target.item.data);
    content.title=item->display_id;
    bool pet=false;for(const auto activate:item->xml.children("Activate"))pet=pet||activate.text()=="PermaPet";
    if(!item->consumable&&!pet&&!item->xml.has("Treasure")&&!item->xml.has("PetFood")&&!item->xml.has("NoTierTag"))
        content.title+=item->tier>=0 ? " (T"+std::to_string(item->tier)+")" : item->xml.attribute("setType").empty() ? " (UT)" : " (ST)";
    content.title_color=modifier_color>=0 ? static_cast<std::uint32_t>(modifier_color) : target.usable ? 0xffffffU : warning_color;
    auto line=[&](std::string text,std::uint32_t color=effect_color,bool bold=false) { content.lines.push_back({std::move(text),color,bold}); };
    const auto description=item->xml.child("Description").text();if(!description.empty())line(description,0xb3b3b3);
    for(const auto extra:item->xml.child("ExtraTooltipData").children("EffectInfo")) {
        const auto name=extra.attribute("name"),detail=extra.attribute("description");
        line(name+(name.empty()||detail.empty() ? "" : ": ")+detail);
    }
    if(!item->projectiles.empty()) {
        const auto& projectile=item->projectiles.front();const double mod=item_data::stat(target.item.data,item_data::damage);
        const int minimum=projectile.min_damage+static_cast<int>(projectile.min_damage*mod),maximum=projectile.max_damage+static_cast<int>(projectile.max_damage*mod);
        if(item->num_projectiles>1)line("Shots: "+std::to_string(item->num_projectiles));
        line("Damage: "+std::to_string(minimum)+(minimum==maximum ? "" : " - "+std::to_string(maximum))+(mod ? " (+"+number(mod*100)+"%)" : ""));
        line("Range: "+number(projectile.motion.speed*projectile.motion.lifetime_ms/10000));
        if(projectile.multi_hit)line("Shots hit multiple targets");
        if(projectile.passes_cover)line("Shots pass through obstacles");
        if(projectile.armor_piercing)line("Armor piercing");
        const double rate=item->xml.number("RateOfFire",1)*(1+item_data::stat(target.item.data,item_data::rate_of_fire));
        line("Rate of fire: "+number(rate*100)+"%");
        for(const auto effect:projectile.xml.children("ConditionEffect"))line("Shot effect: "+effect.text()+" for "+effect.attribute("duration","0")+"s");
    }
    const auto bonus=bonuses(*item,target.item.data);
    for(std::size_t i=0;i<bonus.size();++i)if(bonus[i])line("On equip: "+signed_number(bonus[i])+" "+std::string(stat_names[i]));
    for(const auto activate:item->xml.children("Activate")) {
        const auto kind=activate.text();const auto amount=activate.attribute("amount","0"),duration=activate.attribute("duration","0"),range=activate.attribute("range","0"),radius=activate.attribute("radius","0"),damage=activate.attribute("totalDamage","0"),effect=activate.attribute("effect");
        if(kind=="Heal"||kind=="Magic")line("Use: +"+amount+(kind=="Heal" ? " HP" : " MP"));
        else if(kind=="HealNova"||kind=="MagicNova")line("Party: +"+amount+(kind=="HealNova" ? " HP" : " MP")+" within "+range+" tiles");
        else if(kind=="ConditionEffectSelf"||kind=="ConditionEffectAura")line((kind=="ConditionEffectSelf" ? "Self: " : "Party: ")+effect+" for "+duration+"s"+(kind=="ConditionEffectAura" ? " within "+range+" tiles" : ""));
        else if(kind=="StatBoostSelf"||kind=="StatBoostAura")line((kind=="StatBoostSelf" ? "Self: +" : "Party: +")+amount+" "+stat_name(std::stoi(activate.attribute("stat","-1")))+" for "+duration+"s"+(kind=="StatBoostAura" ? " within "+range+" tiles" : ""));
        else if(kind=="IncrementStat")line("Permanent: +"+amount+" "+stat_name(std::stoi(activate.attribute("stat","-1"))));
        else if(kind=="Teleport")line("Teleport to target"+(activate.attribute("maxDistance").empty() ? "" : " (up to "+activate.attribute("maxDistance")+" tiles)"));
        else if(kind=="VampireBlast"||kind=="PoisonGrenade"||kind=="Trap"||kind=="DazeBlast") {
            line((kind=="VampireBlast" ? "Steal: " : kind=="PoisonGrenade" ? "Poison: " : kind=="Trap" ? "Trap: " : "Blast: ")+damage+" HP within "+radius+" tiles"+(kind=="PoisonGrenade" ? " over "+duration+"s" : ""));
            if(!effect.empty())line(effect+" for "+activate.attribute("condDuration",duration)+"s");
        }
        else if(kind=="Lightning")line("Lightning: "+damage+" damage to "+activate.attribute("maxTargets","0")+" targets");
        else if(kind=="StasisBlast"||kind=="Decoy")line((kind=="StasisBlast" ? "Group stasis: " : "Decoy: ")+duration+"s");
        else if(kind=="BulletNova")line("Nova: 20 shots");
        else if(kind=="ShurikenAbility")line("Stars seek and daze nearby enemies");
        else if(kind=="Dye")line("Changes character colors");
        else if(kind=="Backpack")line("Unlocks eight backpack slots");
        else if(kind=="Create")line("Creates: "+activate.attribute("id"));
        else if(kind=="Pet"||kind=="PermaPet")line("Summons: "+activate.attribute("objectId"));
        else if(kind=="UnlockPortal")line("Unlocks: "+activate.attribute("dungeonName"));
        else if(kind=="RemoveNegativeConditions"||kind=="RemoveNegativeConditionsSelf")line("Removes negative conditions"+(kind=="RemoveNegativeConditions" ? " within "+range+" tiles" : ""));
        else if(kind=="ClearConditionEffectSelf"||kind=="ClearConditionEffectAura")line("Removes: "+effect+(kind=="ClearConditionEffectAura" ? " within "+range+" tiles" : ""));
        else if(kind!="Shoot")line("Use: "+kind);
    }
    if(item->xml.has("MpCost"))line("MP cost: "+std::to_string(item->mp_cost));
    if(item->xml.has("MultiPhase"))line("MP end cost: "+item->xml.child("MpEndCost").text());
    if(item->xml.has("Cooldown")||item_data::stat(target.item.data,item_data::cooldown))line("Cooldown: "+number(item->cooldown_ms/1000.0*(1-item_data::stat(target.item.data,item_data::cooldown)))+"s");
    if(item->xml.has("Doses"))line("Doses: "+item->xml.child("Doses").text());
    const int fame=item->xml.integer("FameBonus")+static_cast<int>(item_data::stat(target.item.data,1U<<19U,1));
    if(fame)line("Fame bonus: "+std::to_string(fame)+"%");
    if(const auto* equipped=item_definition(catalog,target.equipped.type);equipped&&equipped->slot_type==item->slot_type) {
        line("Compared with "+equipped->display_id,0xffffff,true);
        const auto before=bonuses(*equipped,target.equipped.data);bool changed=false;
        auto difference=[&](std::string_view label,double value,bool lower_better=false) {
            if(std::abs(value)<.005)return;
            changed=true;
            line(signed_number(value)+" "+std::string(label),(lower_better ? value<0 : value>0) ? 0x77dd77 : warning_color);
        };
        for(std::size_t i=0;i<bonus.size();++i)difference(stat_names[i],bonus[i]-before[i]);
        if(!item->projectiles.empty()&&!equipped->projectiles.empty()) {
            const auto& a=item->projectiles.front();const auto& b=equipped->projectiles.front();
            const auto average=[](const ProjectileDef& p,int data) { const double mod=item_data::stat(data,item_data::damage);return (p.min_damage+static_cast<int>(p.min_damage*mod)+p.max_damage+static_cast<int>(p.max_damage*mod))/2.0; };
            difference("average damage per shot",average(a,target.item.data)-average(b,target.equipped.data));
            difference("range",(a.motion.speed*a.motion.lifetime_ms-b.motion.speed*b.motion.lifetime_ms)/10000);
            difference("shots",item->num_projectiles-equipped->num_projectiles);
            difference("% rate of fire",100*(item->xml.number("RateOfFire",1)*(1+item_data::stat(target.item.data,item_data::rate_of_fire))-equipped->xml.number("RateOfFire",1)*(1+item_data::stat(target.equipped.data,item_data::rate_of_fire))));
        }
        if(item->usable&&equipped->usable)difference("s cooldown",(item->cooldown_ms*(1-item_data::stat(target.item.data,item_data::cooldown))-equipped->cooldown_ms*(1-item_data::stat(target.equipped.data,item_data::cooldown)))/1000,true);
        if(!changed)line("No change to listed combat stats",0xb3b3b3);
    }
    if(!target.usable) {
        const auto* player=catalog.object(static_cast<std::uint16_t>(target.player_type));
        line("Not usable by "+(player ? player->display_id : "this class"),warning_color,true);
        std::string classes;
        for(const auto& player_class:catalog.objects())if(player_class.player&&std::find(player_class.slot_types.begin(),player_class.slot_types.end(),item->slot_type)!=player_class.slot_types.end()) { if(!classes.empty())classes+=", ";classes+=player_class.display_id; }
        if(!classes.empty())line("Usable by: "+classes,0xb3b3b3);
    }
    for(const auto requirement:item->xml.children("EquipRequirement"))if(requirement.text()=="Stat") {
        const int id=std::stoi(requirement.attribute("stat","-1")),value=std::stoi(requirement.attribute("value","0")),i=stat_index(id);
        const bool met=i>=0&&target.stats[static_cast<std::size_t>(i)]>=value;
        line("Requires "+std::to_string(value)+" "+stat_name(id),met ? 0xb3b3b3 : warning_color,!met);
    }
    if(item->xml.has("Soulbound"))line("Soulbound",0xb3b3b3);
    if(item->consumable)line("Consumed with use",0xb3b3b3);
    if(target.owner==TooltipOwner::merchant)line("Use the purchase button to buy",0xffffff);
    else if(target.owner==TooltipOwner::potion)line((target.item.type==potion_types[0] ? std::string("F") : std::string("V"))+" or Shift/double-click to drink",0xffffff);
    else if(target.owner==TooltipOwner::container)line("Double-click to take"+(item->consumable||item->inventory_use ? std::string("; Shift-click to use") : ""),0xffffff);
    else if(target.usable) {
        if(item->usable)line("Space in the playfield to use when equipped",0xffffff);
        if(target.slot>=4)line(item->consumable||item->inventory_use ? "Double-click or Shift-click to use" : "Double-click to equip",0xffffff);
        if(target.slot>=4&&target.backpack)line("Ctrl-click to transfer to backpack / inventory",0xb3b3b3);
    }
    return content;
}
bool ItemTooltip::update(const std::optional<ItemTooltipTarget>& target) {
    if(!target) { target_.reset();scroll_=0;return false; }
    if(target_==target)return false;
    const auto* definition=item_definition(catalog_,target->item.type);
    if(!definition||definition->slot_type<0) { target_.reset();return false; }
    target_=target;scroll_=0;render(*target);viewport();++rebuilds_;return true;
}
void ItemTooltip::render(const ItemTooltipTarget& target) {
    const auto description=item_tooltip_content(catalog_,target);
    struct Field { TextBitmap bitmap;int x{},y{}; };
    std::vector<Field> fields;int y=10;
    auto text=[&](std::string_view value,int size,bool bold,std::uint32_t color,int x,int available) {
        for(const auto& line:fonts_.wrap(value,size,bold,available)) {
            auto bitmap=fonts_.rasterize(line,size,bold,false,color);
            const int advance=static_cast<int>(std::ceil(bitmap.height))+2;
            fields.push_back({std::move(bitmap),x,y});y+=advance;
        }
    };
    text(description.title,16,true,description.title_color,62,width-72);y=std::max(y,56);
    for(const auto& line:description.lines) { y+=4;text(line.text,14,line.bold,line.color,10,width-20); }
    content_={width,y+10,std::vector<unsigned char>(static_cast<std::size_t>(width)*static_cast<std::size_t>(y+10)*4)};
    fill(content_,target.usable ? 0x363636 : 0x4b3030);
    for(const auto& field:fields)blit(content_,field.bitmap.image,field.x+static_cast<int>(std::floor(field.bitmap.offset.x)),field.y+static_cast<int>(std::floor(field.bitmap.offset.y)));
    const auto key="item"+std::to_string(target.item.type);
    if(art_.contains(key)) { const auto& icon=art_.image(key);blit(content_,icon,10+(44-icon.width)/2,10+(44-icon.height)/2,true); }
}
void ItemTooltip::viewport() {
    height_=std::min(maximum_height,content_.height);std::fill(image_.pixels.begin(),image_.pixels.end(),0);
    const bool overflow=content_.height>maximum_height;const int body=height_-(overflow ? 22 : 0);
    scroll_=std::clamp(scroll_,0,std::max(0,content_.height-body));
    std::copy_n(content_.pixels.begin()+static_cast<std::ptrdiff_t>(scroll_)*width*4,static_cast<std::size_t>(body)*width*4,image_.pixels.begin());
    if(overflow) {
        const auto hint=fonts_.rasterize("Mouse wheel to scroll",14,false,false,0xb3b3b3);
        for(int row=body;row<height_;++row)for(int x=0;x<width;++x) {
            const auto at=static_cast<std::size_t>(row*width+x)*4;image_.pixels[at]=image_.pixels[at+1]=image_.pixels[at+2]=0x28;image_.pixels[at+3]=255;
        }
        blit(image_,hint.image,10+static_cast<int>(hint.offset.x),body+static_cast<int>(hint.offset.y));
    }
    const std::uint32_t border=target_->usable ? 0x9b9b9b : 0xa75b5b;
    for(int y=0;y<height_;++y)for(int x=0;x<width;++x)if(x==0||x==width-1||y==0||y==height_-1) {
        const auto at=static_cast<std::size_t>(y*width+x)*4;image_.pixels[at]=static_cast<unsigned char>(border>>16U);image_.pixels[at+1]=static_cast<unsigned char>(border>>8U);image_.pixels[at+2]=static_cast<unsigned char>(border);image_.pixels[at+3]=255;
    }
}
bool ItemTooltip::scroll(int steps) {
    if(!visible()||content_.height<=maximum_height||steps==0)return false;
    const int next=static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(scroll_)-static_cast<std::int64_t>(steps)*36,0,content_.height-(height_-22)));
    if(next==scroll_)return false;
    scroll_=next;viewport();return true;
}
Vec2 ItemTooltip::position(Vec2 pointer,int window_width,int window_height,int tooltip_height,double scale) {
    const double margin=std::min(4.0*scale,std::min(window_width,window_height)/2.0);
    const double x=pointer.x<window_width/2.0 ? pointer.x+12*scale : pointer.x-(width+12)*scale;
    return {std::clamp(x,margin,std::max(margin,window_width-width*scale-margin)),std::clamp(pointer.y+12*scale,margin,std::max(margin,window_height-tooltip_height*scale-margin))};
}
}
