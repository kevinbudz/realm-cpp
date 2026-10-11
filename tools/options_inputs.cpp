#include "realm/options.hpp"
#include "realm/image.hpp"
#include "realm/text_tooltip.hpp"
#include "realm/ui_draw.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
// Renders tests/options_cases.json through the native OptionsScreen, for comparison with Flash's own Options.
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string(argv[1])=="--table") {
            // The option table, for tests/options_source_tests.py.
            nlohmann::json table=nlohmann::json::array();
            for(const auto& option:realm::option_defs()) {
                nlohmann::json choices=nlohmann::json::array();
                for(const auto& [label,value]:option.choices)choices.push_back({std::string(label),value});
                table.push_back({{"tab",static_cast<int>(option.tab)},{"kind",static_cast<int>(option.kind)},{"param",std::string(option.param)},{"label",std::string(option.label)},
                    {"tooltip",std::string(option.tooltip)},{"default",option.fallback},{"choices",choices},{"enabled_by",std::string(option.enabled_by)}});
            }
            std::cout<<table.dump(1)<<'\n';return 0;
        }
        if(argc!=5&&argc!=6)throw std::runtime_error("realm_options_inputs FONT_DIR FLASH_ROOT CASES OUTPUT [TOOLTIPS]");
        realm::FontBook fonts(argv[1]);fonts.enable_native(argv[2]);
        nlohmann::json cases;std::ifstream(argv[3])>>cases;
        const std::filesystem::path out=argv[4];std::filesystem::create_directories(out);
        nlohmann::json metrics=nlohmann::json::array();
        for(const auto& c:cases) {
            realm::Settings settings;
            for(const auto& [name,value]:c.at("values").items()) {
                const auto* option=realm::find_option(name);if(!option)throw std::runtime_error("Unknown option "+name);
                const int number=value.is_boolean() ? (value.get<bool>() ? 1 : 0) : value.get<int>();
                if(option->kind==realm::OptionDef::Kind::key)settings.set_key(name,number);else settings.set_choice(name,number);
            }
            const int w=c.at("width"),h=c.at("height");fonts.set_pixel_scale(h/600.0);
            realm::OptionsScreen screen;screen.open(settings);screen.update(fonts,w,h,0);
            screen.set_tab(static_cast<realm::OptionsTab>(c.at("tab").get<int>()));screen.update(fonts,w,h,0);
            if(c.contains("hover")) {
                const auto& hover=c.at("hover");const std::string kind=hover.at("kind");const int index=hover.at("index");
                realm::HudRect r;
                if(kind=="tab")r=screen.tab_rect(index);else if(kind=="button")r=screen.button_rect(index);
                else if(kind=="box")r=screen.box_rect(index);else r=screen.label_rect(index);
                realm::Vec2 at{screen.position().x+(r.x+r.width/2)*screen.scale(),(r.y+r.height/2)*screen.scale()};
                // The Flash fixture points 16 units into the label's row at x+120 (a point on every label).
                if(kind=="label")at={screen.position().x+(screen.box_rect(index).x+120)*screen.scale(),(screen.box_rect(index).y+16)*screen.scale()};
                screen.pointer_move(at);
                screen.update(fonts,w,h,0);
            }
            auto result=realm::ui_image(w,h,1);realm::ui_rectangle(result,0,0,w,h,0x363636);
            auto surface=screen.image();surface.pixel_scale=1;realm::ui_blit(result,surface,0,0);
            nlohmann::json rects=nlohmann::json::array();
            for(std::size_t i=0;i<screen.option_count();++i) {
                const auto r=screen.box_rect(static_cast<int>(i));rects.push_back({{"x",r.x},{"y",r.y}});
            }
            metrics.push_back({{"key",c.at("key")},{"cells",screen.option_count()},{"rects",rects},{"tooltip",screen.tooltip_visible()}});
            realm::save_png(out/(c.at("key").get<std::string>()+".png"),result);
        }
        if(argc==6) {
            nlohmann::json tips;std::ifstream(argv[5])>>tips;fonts.set_pixel_scale(1);
            for(const auto& t:tips) {
                const auto tip=realm::render_text_tooltip(fonts,t.at("title").get<std::string>(),t.at("text").get<std::string>(),t.at("background"),t.at("outline"),t.at("width"));
                auto result=realm::ui_image(300,200,1);realm::ui_rectangle(result,0,0,300,200,0x363636);
                // ToolTip.position() puts the origin at (12, 12) for a pointer at the stage's origin.
                auto surface=tip.image;surface.pixel_scale=1;realm::ui_blit(result,surface,6-tip.margin,6-tip.margin);
                metrics.push_back({{"key",t.at("key")},{"frame_width",tip.frame_width},{"frame_height",tip.frame_height}});
                realm::save_png(out/(t.at("key").get<std::string>()+".png"),result);
            }
        }
        std::ofstream(out/"metrics.json")<<metrics.dump(2)<<'\n';
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
