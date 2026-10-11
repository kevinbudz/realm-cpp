#include "realm/trade.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
// Renders TradePanel and TradeRequestPanel fixtures in the Flash reference framing.
int main(int argc,char** argv) {
try {
    if(argc<5||argc>6)throw std::runtime_error("Usage: realm_trade_inputs FONTS HUD_ART CASES OUTPUT [SCALE]");
    realm::Catalog catalog(realm::flash::source_root);realm::FontBook fonts(argv[1]);realm::HudArt art(argv[2]);
    if(argc==6) { fonts.enable_native(realm::flash::source_root);fonts.set_pixel_scale(std::stod(argv[5])); }
    std::ifstream file(argv[3]);const auto cases=nlohmann::json::parse(file);const std::filesystem::path output(argv[4]);std::filesystem::create_directories(output);
    const auto save=[&](const realm::Image& surface,double crop_top,double height,const std::string& name) {
        auto frame=realm::ui_image(200,height,fonts.pixel_scale());
        realm::ui_blit(frame,surface,0,-crop_top,false,false);
        realm::save_png(output/(name+".png"),frame);
    };
    for(const auto& task:cases) {
        realm::net::incoming::TradeStart start;
        for(int side=0;side<2;++side)for(const auto& slot:task.at("sides").at(side).at("items")) {
            const std::string id=slot.at(0);
            realm::net::TradeItem item{id.empty() ? -1 : static_cast<std::int32_t>(catalog.object(id)->type),slot.at(1),slot.at(2),slot.at(3)};
            (side==0 ? start.my_items : start.their_items).push_back(item);
        }
        start.their_name=task.at("sides").at(1).at("name");
        realm::Trade trade;trade.start(start,task.at("sides").at(0).at("name"),0);
        if(task.at("button")=="waiting") {
            trade.update(realm::Trade::wait_time);
            if(!trade.accept(realm::Trade::wait_time))throw std::runtime_error("Accept fixture could not accept");
            realm::net::incoming::TradeAccepted accepted{realm::Trade::offer(trade.mine()),realm::Trade::offer(trade.theirs())};
            trade.accepted(accepted);
        }
        realm::TradeView view(fonts,art);view.update(trade,{-100,-100},0);
        save(view.panel(),realm::TradeView::origin_y-realm::TradeView::panel_top,400,task.at("id"));
    }
    for(const auto& [name,id]:std::array<std::pair<std::string,std::string>,2>{{{"Friend","request"},{"Abcdefghij","request-long"}}}) {
        realm::Trade trade;trade.requested(name,0);realm::TradeView view(fonts,art);view.update(trade,{-100,-100},0);
        save(view.request(),0,100,id);
    }
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
