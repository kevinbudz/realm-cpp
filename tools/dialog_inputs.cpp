#include "realm/dialog.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    try {
        if(argc!=5)throw std::runtime_error("realm_dialog_inputs FONT_DIR FLASH_ROOT CASES OUTPUT");
        realm::FontBook fonts(argv[1]);fonts.enable_native(argv[2]);
        nlohmann::json cases;std::ifstream(argv[3])>>cases;
        std::filesystem::create_directories(argv[4]);nlohmann::json metrics=nlohmann::json::array();
        for(const auto& c:cases) {
            realm::Dialog dialog;realm::Dialog::Content content{c.at("body"),std::nullopt,{}};
            if(!c.at("title").is_null())content.title=c.at("title").get<std::string>();
            for(std::size_t i=0;i<2;++i)if(!c.at("buttons")[i].is_null())content.buttons[i]=c.at("buttons")[i].get<std::string>();
            if(c.at("messageClose").get<bool>())dialog.message_close(*content.title,content.body,*content.buttons[0]);else dialog.show(content);
            const int width=c.at("width"),height=c.at("height");fonts.set_pixel_scale(height/600.0);
            if(c.at("disabled").get<int>()>=0)dialog.set_enabled(c.at("disabled"),false);
            dialog.update(fonts,width,height);
            if(c.at("hover").get<int>()>=0) { const auto b=dialog.button(c.at("hover"));const auto p=dialog.position();dialog.pointer_move({p.x+(b.x+5)*dialog.scale(),p.y+(b.y+5)*dialog.scale()});dialog.update(fonts,width,height); }
            auto image=realm::ui_image(width,height,1);realm::ui_rectangle(image,0,0,width,height,0x363636);
            if(c.at("backdrop").get<bool>()) {
                auto dim=realm::ui_image(width,height,1);realm::ui_rectangle(dim,0,0,width,height,0x151515);realm::ui_blit(image,dim,0,0,false,false,.6);
            }
            auto surface=dialog.image();surface.pixel_scale=1;const auto at=dialog.canvas_position();realm::ui_blit(image,surface,at.x,at.y);
            realm::save_png(std::filesystem::path(argv[4])/(c.at("key").get<std::string>()+".png"),image);
            metrics.push_back({{"key",c.at("key")},{"height",dialog.height()},{"buttonY",dialog.button(0).y},{"position",{dialog.position().x,dialog.position().y}},{"image",{dialog.image().width,dialog.image().height}}});
        }
        std::ofstream(std::filesystem::path(argv[4])/"metrics.json")<<metrics.dump(2)<<'\n';
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
