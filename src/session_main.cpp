#include "realm/session.hpp"
#include "flash_assets.hpp"
#include "realm/movement.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
namespace {
int value(std::string_view s) { std::size_t count{}; const auto number = std::stoi(std::string(s), &count, 0); if (count != s.size()) throw std::invalid_argument("Invalid number"); return number; }
}
int main(int argc, char** argv) {
    try {
        realm::SessionOptions options; int seconds = 10; bool password_stdin{}, exercise{},exercise_inventory{},exercise_potion{},exercise_portal{};
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i]; const auto next = [&] { if (++i >= argc) throw std::runtime_error("Missing option value"); return std::string(argv[i]); };
            if (arg == "--server") options.host = next();
            else if (arg == "--port") { const auto port = value(next()); if (port < 1 || port > 65535) throw std::runtime_error("Invalid port"); options.port = static_cast<std::uint16_t>(port); }
            else if (arg == "--username") options.username = next();
            else if (arg == "--character") options.character_id = value(next());
            else if (arg == "--create") { options.create_character = true; const auto type = value(next()); if (type < 0 || type > 65535) throw std::runtime_error("Invalid class type"); options.class_type = static_cast<std::uint16_t>(type); }
            else if (arg == "--game") options.game_id = value(next());
            else if (arg == "--seconds") seconds = value(next());
            else if (arg == "--password-stdin") password_stdin = true;
            else if (arg == "--exercise") exercise = true;
            else if (arg == "--exercise-inventory") exercise_inventory=true;
            else if (arg == "--exercise-potion") exercise_potion=true;
            else if (arg == "--exercise-portal") exercise_portal=true;
            else throw std::runtime_error("Usage: realm_session [--server HOST] [--port N] [--username NAME] [--password-stdin] [--character N|--create TYPE] [--game N] [--seconds N]");
        }
        if (seconds < 1 || seconds > 86400) throw std::runtime_error("Invalid session duration");
        if(static_cast<int>(exercise)+exercise_inventory+exercise_potion+exercise_portal>1)throw std::runtime_error("Choose one exercise mode");
        if (password_stdin) std::getline(std::cin, options.password);
        realm::Catalog catalog(realm::flash::source_root); realm::World world(catalog); realm::Session session(world);
        session.connect(std::move(options)); const auto epoch = std::chrono::steady_clock::now();
        auto previous = epoch; bool playing{}; std::int32_t ability_time{};
        int inventory_stage{},source_slot{-1},target_slot{-1};realm::InventoryItem original_item,dropped_item;
        int potion_stage{},potion_start{},potion_results{},potion_expected{1},portal_stage{};std::int32_t portal_id{-1};
        const auto stat=[](const realm::Entity& entity,std::uint8_t id) { const auto found=entity.stats.find(id);return found==entity.stats.end()?-1:found->second; };
        while (std::chrono::steady_clock::now() - epoch < std::chrono::seconds(seconds)) {
            const auto now = std::chrono::steady_clock::now();
            const auto time = static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - epoch).count());
            const auto dt = std::chrono::duration<double,std::milli>(now - previous).count(); previous = now;
            session.poll(time); world.update(dt);
            if(exercise_inventory&&inventory_stage==0&&session.state()==realm::SessionState::playing)if(const auto* player=world.entity(world.player_id())) {
                for(int i=4;i<12;++i) {
                    const auto item=realm::inventory_item(*player,i);
                    if(item.type==-1&&target_slot<0)target_slot=i;
                    if(item.type>0&&item.type<=65535)if(const auto* def=catalog.object(static_cast<std::uint16_t>(item.type));def&&def->consumable&&source_slot<0) {source_slot=i;original_item=item;}
                }
                if(source_slot>=0&&target_slot>=0) {session.swap_items({player->id,static_cast<std::uint8_t>(source_slot)},{player->id,static_cast<std::uint8_t>(target_slot)},time);inventory_stage=1;}
            }
            // The dropped weapon returns in a soulbound bag; pick it up as a
            // ContainerGrid double-click would, then re-equip it.
            if(exercise_inventory&&inventory_stage==4)if(const auto* player=world.entity(world.player_id()))if(const auto* bag=world.interactive_target()) {
                const auto* def=catalog.object(bag->type);
                for(int i=0;def&&def->container&&i<8;++i)if(realm::inventory_item(*bag,i)==dropped_item) {
                    session.equip_or_use({bag->id,static_cast<std::uint8_t>(i)},time);
                    for(int j=4;j<12;++j)if(realm::inventory_item(*player,j)==dropped_item)target_slot=j;
                    if(target_slot<0)throw std::runtime_error("Container pickup was not predicted");
                    inventory_stage=5;break;
                }
            }
            // Stat updates queued before an accepted swap can arrive ahead of its
            // INVRESULT, so the exercise waits for the authoritative slots.
            if(exercise_inventory&&inventory_stage==6)if(const auto* player=world.entity(world.player_id());player&&realm::inventory_item(*player,target_slot)==dropped_item) {
                session.equip_or_use({player->id,static_cast<std::uint8_t>(target_slot)},time);
                if(realm::inventory_item(*player,0)!=dropped_item)throw std::runtime_error("Re-equip was not predicted");
                inventory_stage=7;
            }
            if(exercise_inventory&&inventory_stage==8)if(const auto* player=world.entity(world.player_id());player&&realm::inventory_item(*player,0)==dropped_item) {
                inventory_stage=9;std::cout<<"Real server inventory SWAP, consumable USEITEM, DROP, container pickup and re-equip accepted\n";
            }
            // Spend MP with the starter ability unless an earlier session left it
            // below maximum, then refill it from the magic stack.
            if(exercise_potion&&session.state()==realm::SessionState::playing)if(const auto* player=world.entity(world.player_id())) {
                if(potion_stage==0&&stat(*player,60)>0&&stat(*player,3)>0) {
                    potion_start=stat(*player,60);
                    if(stat(*player,4)>=stat(*player,3)) { session.use_ability({player->position.x+3,player->position.y},time);potion_expected=2; }
                    potion_stage=1;
                }
                else if(potion_stage==1&&stat(*player,4)<stat(*player,3)) {
                    session.use_potion(1,time);
                    if(stat(*player,60)!=potion_start-1)throw std::runtime_error("Magic potion was not predicted");
                    potion_stage=2;
                } else if(potion_stage==3&&stat(*player,4)==stat(*player,3)&&stat(*player,60)==potion_start-1) { potion_stage=4;std::cout<<"Real server magic potion USEITEM accepted; stack "<<potion_start<<" -> "<<potion_start-1<<"\n"; }
            }
            // Walk to the Nexus vault portal until its PortalPanel would show, then enter it.
            if(exercise_portal&&session.state()==realm::SessionState::playing)if(auto* player=world.entity(world.player_id())) {
                if(portal_stage==0) {
                    double best=1e9;
                    for(const auto& e:world.entities())if(const auto* def=catalog.object(e.type);def&&def->class_name=="Portal"&&def->id=="Vault Portal") {
                        const double distance=std::hypot(e.position.x-player->position.x,e.position.y-player->position.y);if(distance<best) { best=distance;portal_id=e.id; }
                    }
                    if(portal_id>=0)portal_stage=1;
                } else if(portal_stage==1) {
                    const auto* portal=world.entity(portal_id);if(!portal)throw std::runtime_error("Vault portal disappeared");
                    if(const auto* target=world.interactive_target();target&&target->id==portal_id) {
                        const auto active=target->stats.find(static_cast<std::uint8_t>(realm::flash::StatId::ACTIVE_STAT));
                        if(active!=target->stats.end()&&active->second==0)throw std::runtime_error("Vault portal is inactive");
                        std::cout<<"Interaction target "<<(target->name.empty() ? catalog.object(target->type)->display_id : target->name)<<" entered\n";session.use_portal(portal_id);portal_stage=2;
                    } else {
                        double angle{};const double dx=portal->position.x-player->position.x,dy=portal->position.y-player->position.y,length=std::hypot(dx,dy);
                        realm::move_player(world,*player,{dx/length,dy/length},0,dt,angle);
                    }
                } else if(portal_stage==2&&world.name()!="Nexus") { portal_stage=3;std::cout<<"Real server USEPORTAL reconnected to map="<<world.name()<<"\n"; }
            }
            if (exercise && session.state() == realm::SessionState::playing) if (auto* player = world.entity(world.player_id())) {
                double angle{};
                realm::move_player(world,*player,{time % 2000 < 1000 ? 1.0 : -1.0,0},0,dt,angle);
                const realm::Vec2 ability_target{player->position.x+3,player->position.y};
                session.shoot(0,time);
                if(time>=ability_time) { session.use_ability(ability_target,time);ability_time=time+2000; }
            }
            session.finish_frame(time);
            for (const auto& event : session.take_events()) {
                std::cout << "packet=" << static_cast<int>(event.id) << '\n';
                if (event.id == realm::flash::PacketId::TEXT) std::cout << std::get<realm::net::incoming::Text>(event.data).text << '\n';
                if(exercise_potion&&event.id==realm::flash::PacketId::INVRESULT) {
                    if(std::get<realm::net::incoming::InvResult>(event.data).result!=0)throw std::runtime_error("Real server rejected potion exercise");
                    if(++potion_results==potion_expected&&potion_stage==2)potion_stage=3;
                }
                if(exercise_inventory&&event.id==realm::flash::PacketId::INVRESULT) {
                    if(std::get<realm::net::incoming::InvResult>(event.data).result!=0)throw std::runtime_error("Real server rejected inventory exercise");
                    const auto* player=world.entity(world.player_id());if(!player)throw std::runtime_error("Inventory fixture player disappeared");
                    if(inventory_stage==1) {
                        if(realm::inventory_item(*player,target_slot)!=original_item)throw std::runtime_error("Real server swap differs from prediction");
                        session.use_item({player->id,static_cast<std::uint8_t>(target_slot)},time);inventory_stage=2;
                    } else if(inventory_stage==2) {
                        if(realm::inventory_item(*player,target_slot).type!=-1)throw std::runtime_error("Real server consumable differs from prediction");
                        if(realm::inventory_item(*player,0).type==-1)throw std::runtime_error("Isolated fixture needs starter weapon");
                        dropped_item=realm::inventory_item(*player,0);session.drop_to_ground(0,time);inventory_stage=3;
                    } else if(inventory_stage==3) {
                        if(realm::inventory_item(*player,0).type!=-1)throw std::runtime_error("Real server drop differs from prediction");
                        inventory_stage=4;target_slot=-1;
                    } else if(inventory_stage==5||inventory_stage==7)++inventory_stage;
                }
            }
            if (session.state() == realm::SessionState::playing && !playing) { std::cout << "Playing character=" << session.character_id() << " map=" << world.name() << '\n'; playing = true; }
            if (session.state() == realm::SessionState::failed || session.state() == realm::SessionState::disconnected) throw std::runtime_error(session.error());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(exercise_inventory&&inventory_stage!=9)throw std::runtime_error("Inventory exercise incomplete at stage "+std::to_string(inventory_stage));
        if(exercise_portal&&portal_stage!=3)throw std::runtime_error("Portal exercise incomplete at stage "+std::to_string(portal_stage));
        if(exercise_potion&&potion_stage!=4)throw std::runtime_error("Potion exercise incomplete at stage "+std::to_string(potion_stage));
        session.disconnect(); std::cout << "World " << world.width() << 'x' << world.height() << " objects=" << world.entities().size() << '\n';
    } catch (const std::exception& error) { std::cerr << "realm_session: " << error.what() << '\n'; return 1; }
}
