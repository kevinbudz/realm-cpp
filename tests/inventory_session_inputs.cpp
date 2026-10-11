#include "realm/session.hpp"
#include "flash_assets.hpp"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc,char** argv) {
try {
    if(argc!=2)throw std::runtime_error("Expected loopback port");
    realm::Catalog catalog(realm::flash::source_root);realm::World world(catalog);realm::Session session(world);
    realm::SessionOptions options;options.host="localhost";options.port=static_cast<std::uint16_t>(std::stoi(argv[1]));options.username="fixture";options.password="test-password";options.character_id=7;session.connect(options);
    const auto start=std::chrono::steady_clock::now();int stage{};
    while(stage<5) {
        const auto time=static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());
        if(time>5000)throw std::runtime_error("Inventory loopback timeout");
        session.poll(time);
        for(const auto& event:session.take_events())if(event.id==realm::flash::PacketId::INVRESULT) {
            const auto* player=world.entity(99);if(!player)throw std::runtime_error("Missing fixture player");
            if(stage==1&&realm::inventory_item(*player,4).data!=111)throw std::runtime_error("Session swap rollback failed");
            if(stage==2&&realm::inventory_item(*player,6).type!=0xa22)throw std::runtime_error("Session consumable rollback failed");
            if(stage==4&&realm::inventory_item(*player,4).data!=111)throw std::runtime_error("Ability/drop FIFO mismatch");
            ++stage;
            if(stage==2)session.use_item({99,6},time);
            if(stage==3)session.use_ability({4,4},time);
            if(stage==4)session.drop_item(4);
        }
        if(stage==0&&session.state()==realm::SessionState::playing&&world.entity(99)) {session.swap_items({99,4},{99,0},time);stage=1;}
        if(session.state()==realm::SessionState::failed||session.state()==realm::SessionState::disconnected)throw std::runtime_error(session.error());
        session.finish_frame(time);std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.disconnect();std::cout<<"Session inventory prediction and ability FIFO rollback passed\n";
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
