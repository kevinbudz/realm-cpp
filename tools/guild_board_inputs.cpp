#include "realm/guild_board.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    try {
        if(argc!=5)throw std::runtime_error("realm_guild_board_inputs FONT_DIR FLASH_ROOT CASES OUTPUT");
        realm::FontBook fonts(argv[1]);fonts.enable_native(argv[2]);
        nlohmann::json cases;std::ifstream(argv[3])>>cases;
        std::filesystem::create_directories(argv[4]);nlohmann::json metrics=nlohmann::json::array();
        for(const auto& c:cases) {
            realm::GuildBoard board;board.show(c.at("text"),c.at("rank"));
            const int width=c.at("width"),height=c.at("height");fonts.set_pixel_scale(height/600.0);board.update(fonts,width,height);
            const auto click=[&](int i) { const auto b=board.button(i);const auto p=board.position();const realm::Vec2 at{p.x+(b.x+5)*board.scale(),p.y+(b.y+5)*board.scale()};board.pointer_down(at);board.pointer_up(at);board.update(fonts,width,height); };
            if(c.at("edit").get<bool>())click(0);
            if(c.at("scroll").get<double>()>0) {
                auto at=board.position();const auto s=board.scale();
                board.pointer_down({at.x+410*s,at.y+30*s});board.pointer_move({at.x+410*s,at.y+500*s});board.pointer_up({at.x+410*s,at.y+500*s});
            }
            if(c.at("hover").get<int>()>=0) { const auto b=board.button(c.at("hover"));const auto p=board.position();board.pointer_move({p.x+(b.x+5)*board.scale(),p.y+(b.y+5)*board.scale()}); }
            board.update(fonts,width,height);
            auto image=realm::ui_image(width,height,1);realm::ui_rectangle(image,0,0,width,height,0x0b0b0b);
            auto surface=board.image();surface.pixel_scale=1;const auto at=board.canvas_position();realm::ui_blit(image,surface,at.x,at.y);
            realm::save_png(std::filesystem::path(argv[4])/(c.at("key").get<std::string>()+".png"),image);
            metrics.push_back({{"key",c.at("key")},{"width",board.width()},{"height",board.height()}});
        }
        std::ofstream(std::filesystem::path(argv[4])/"metrics.json")<<metrics.dump(2)<<'\n';
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
