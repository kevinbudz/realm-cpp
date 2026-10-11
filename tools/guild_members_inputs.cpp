#include "realm/guild_members.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    try {
        if(argc!=6)throw std::runtime_error("realm_guild_members_inputs FONT_DIR FLASH_ROOT ART_DIR CASES OUTPUT");
        realm::FontBook fonts(argv[1]);fonts.enable_native(argv[2]);
        nlohmann::json cases;std::ifstream(argv[4])>>cases;std::filesystem::create_directories(argv[5]);
        nlohmann::json metrics=nlohmann::json::array();
        for(const auto& c:cases) {
            realm::GuildMemberList list;list.name=c.at("guild");list.fame=c.at("fame");
            for(const auto& m:c.at("members"))list.members.push_back({m.at("name"),m.at("rank"),m.at("fame")});
            realm::GuildMembers view(argv[3]);view.show(std::move(list),c.at("own"),c.at("rank"));
            if(c.value("loading",false))view.open("127.0.0.1",0,{},c.at("guild"),c.at("own"),c.at("rank"));
            const int w=c.at("width"),h=c.at("height");fonts.set_pixel_scale(h/600.0);view.update(fonts,w,h);
            for(int i=0;i<1000&&c.value("scroll",0.0)>0;++i)view.wheel(-1);
            view.update(fonts,w,h);
            const auto at=view.position();const double s=view.scale();
            if(c.contains("action")) { const int a=c.at("action"),row=c.value("row",0);const realm::Vec2 p{at.x+(a==0 ? 686 : a==1 ? 716 : 750)*s,(126+row*32)*s};view.pointer_down(p);view.pointer_up(p); }
            if(c.contains("hover_action"))view.pointer_move({at.x+686*s,(126+c.value("row",0)*32)*s});
            if(c.value("hover",false))view.pointer_move({at.x+400*s,540*s});
            view.update(fonts,w,h);
            auto result=realm::ui_image(w,h,1);realm::ui_rectangle(result,0,0,w,h,0x363636);
            auto surface=view.image();surface.pixel_scale=1;realm::ui_blit(result,surface,0,0);
            metrics.push_back({{"key",c.at("key")},{"members",c.value("loading",false) ? 0 : view.list().members.size()},{"height",view.document_height()}});
            realm::save_png(std::filesystem::path(argv[5])/(c.at("key").get<std::string>()+".png"),result);
        }
        std::ofstream(std::filesystem::path(argv[5])/"metrics.json")<<metrics.dump(2)<<'\n';
    }catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
