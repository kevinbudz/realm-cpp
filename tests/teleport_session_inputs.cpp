#include "realm/player_group.hpp"
#include "realm/session.hpp"
#include "flash_assets.hpp"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc,char** argv) {
try {
    if(argc<3)throw std::runtime_error("Expected mode/port [actor,new target,old target]");
    using namespace realm;const std::string mode=argv[1];Catalog catalog(flash::source_root);World world(catalog);Session session(world);
    SessionOptions options;options.port=static_cast<std::uint16_t>(std::stoi(argv[2]));options.character_id=0;
    options.username=argc>3 ? argv[3] : "Fixture";std::getline(std::cin,options.password);
    if(mode=="hall")options.game_id=-3;
    session.connect(options);const auto start=std::chrono::steady_clock::now();int stage=0,gotos=0,first_time=0;std::vector<PlayerMenuTarget> rows;int selected=-1;
    while(stage<3) {
        const auto time=static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());
        if(time>17000)throw std::runtime_error("Teleport exercise timeout, stage "+std::to_string(stage));
        session.poll(time);
        for(const auto& e:session.take_events())if(e.id==flash::PacketId::GOTO&&std::get<net::incoming::Goto>(e.data).object_id==world.player_id())++gotos;
        if(stage==0&&session.state()==SessionState::playing&&world.entity(world.player_id())) {
            if(mode=="hall"||mode=="forbidden") {
                if(session.map_info().allow_teleport)throw std::runtime_error("Expected map teleport restriction");
                const auto refusal=session.teleport(2,time);if(!refusal||*refusal!="Teleporting is not allowed in this map.")throw std::runtime_error("Map guard failed");
                if(session.teleport_cooldown(time)!=0)throw std::runtime_error("Restricted attempt consumed cooldown");
                stage=3;
            } else {
                int newer=-1,older=-1;
                for(const auto& p:world.entities())if(p.player&&p.id!=world.player_id()) {
                    if(p.name==(argc>4 ? argv[4] : "Newer"))newer=p.id;
                    if(p.name==(argc>5 ? argv[5] : "Older"))older=p.id;
                }
                if(newer>=0&&older>=0) {
                    const std::array ids{newer,older};rows=player_group_targets(world,ids);
                    if(!(world.entity(newer)->condition&invisible_condition)||world.entity(older)->condition&invisible_condition)throw std::runtime_error("Expected staggered target visibility");
                    const auto target=first_teleport_player(world,rows);if(target!=older)throw std::runtime_error("Did not choose first visible target");
                    if(session.teleport(*target,time))throw std::runtime_error("Initial teleport refused");
                    selected=older;first_time=time;
                    const auto refusal=session.teleport(*target,time);if(!refusal||*refusal!="You can not teleport for another 11 seconds.")throw std::runtime_error("Immediate cooldown refusal");stage=1;
                }
            }
        }
        if(stage==1&&gotos==1&&time-first_time>=10000) {
            const auto target=first_teleport_player(world,rows);if(!target||*target==selected)throw std::runtime_error("Live eligibility/order did not refresh");
            if(session.teleport(*target,time))throw std::runtime_error("Expired cooldown blocked teleport");
            stage=2;
        }
        if(stage==2&&gotos==2)stage=3;
        if(session.state()==SessionState::failed||session.state()==SessionState::disconnected)throw std::runtime_error(session.error());
        session.finish_frame(time);std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.poll(static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()));
    session.disconnect();std::cout<<mode<<": native map flag / live ordered eligibility / 10-second cooldown checks passed; GOTO="<<gotos<<'\n';
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
