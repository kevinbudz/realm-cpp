#include "realm/chat.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
// Renders chat fixtures into full 600x600 TextBox frames for review against Flash.
int main(int argc,char** argv) {
try {
    if(argc<5||argc>6)throw std::runtime_error("Usage: realm_chat_inputs FONTS HUD_ART CASES OUTPUT [SCALE]");
    realm::FontBook fonts(argv[1]);realm::HudArt art(argv[2]);
    if(argc==6) { fonts.enable_native(realm::flash::source_root);fonts.set_pixel_scale(std::stod(argv[5])); }
    std::ifstream file(argv[3]);const auto cases=nlohmann::json::parse(file);const std::filesystem::path output(argv[4]);std::filesystem::create_directories(output);
    if(!cases.empty()&&cases.front().contains("colors")) {
        // Speech balloons: the field origin lands at (40,40), as in the Flash capture.
        realm::Catalog catalog(realm::flash::source_root);
        for(const auto& task:cases) {
            realm::World world(catalog);world.initialize(1,1);world.add(1,task.at("type"),{});
            realm::SpeechBalloons balloons;
            const std::string recipient=task.at("id")=="tell" ? "Me" : task.at("id")=="guild" ? "*Guild*" : "";
            balloons.add(world,realm::net::incoming::Text{"X",1,-1,5,recipient,task.at("text")});
            balloons.update(world,fonts,0);
            auto frame=realm::ui_image(240,160,fonts.pixel_scale());
            const auto& balloon=balloons.balloons().front();
            realm::ui_blit(frame,balloon.image,40-realm::SpeechBalloons::margin,40-realm::SpeechBalloons::margin,false,false);
            realm::save_png(output/(task.at("id").get<std::string>()+".png"),frame);
            std::cout<<task.at("id").get<std::string>()<<" w="<<balloon.field_width<<" full="<<balloon.full_height<<" tex="<<balloon.texture_height<<'\n';
        }
        return 0;
    }
    for(const auto& task:cases) {
        realm::Chat chat;realm::ChatView view(fonts,&art);
        for(const auto& line:task.at("lines")) {
            const std::string recipient=line.at("recipient");
            // Outgoing tells come from the local player "Me".
            const bool to_me=line.at("toMe");
            chat.add(realm::net::incoming::Text{line.at("name"),-1,line.at("stars"),0,recipient,line.at("text")},to_me ? recipient : recipient.empty()||recipient=="*Guild*" ? "Me" : line.at("name").get<std::string>(),0);
        }
        if(!task.at("input").is_null()) { chat.open(task.at("input").get<std::string>()); }
        view.update(chat,0);
        auto frame=realm::ui_image(600,600,fonts.pixel_scale());
        if(!view.empty())realm::ui_blit(frame,view.image(),0,view.top(),false,false);
        realm::save_png(output/(task.at("id").get<std::string>()+".png"),frame);
    }
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
