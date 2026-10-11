#include "realm/skins.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
 try {
  realm::FontBook fonts(argv[1]);fonts.enable_native(realm::flash::source_root);realm::HudArt art(argv[2]);realm::Catalog catalog(realm::flash::source_root);
  const auto cases=nlohmann::json::parse(std::ifstream(argv[3]));const std::filesystem::path output=argv[4];std::filesystem::create_directories(output);nlohmann::json metrics=nlohmann::json::array();
  for(const auto& c:cases) {
   const int type=c.at("type");realm::CharList list;list.owned_skins=c.at("owned").get<std::vector<int>>();list.credits=c.value("credits",0);list.fame=c.value("accountFame",0);list.best_fame[type]=c.value("fame",0);list.best_level[type]=c.value("level",0);fonts.set_pixel_scale(c.at("height").get<double>()/600);
   realm::SkinChooser chooser(catalog,fonts,art);chooser.open(type,c.at("selected"),list,c.value("startup",false));chooser.update(c.at("width"),c.at("height"),0);
   if(c.contains("hover_row")) { const auto at=chooser.list_position();const auto scale=fonts.pixel_scale();chooser.pointer_move({at.x+100*scale,at.y+(c.at("hover_row").get<int>()*65+10)*scale}); }
   if(c.contains("hover_button")) { const auto scale=fonts.pixel_scale();chooser.pointer_move({(c.at("width").get<double>()-462*scale)/2+(c.at("hover_button")==0 ? 100 : 330)*scale,(c.at("height").get<double>()-chooser.view_height()*scale)/2+(chooser.view_height()-25)*scale}); }
   if(c.contains("error")||c.value("insufficient",false))chooser.purchase_error(c.value("error",std::string{}),c.value("insufficient",false));
   chooser.update(c.at("width"),c.at("height"),0);
   auto image=realm::ui_image(c.at("width").get<double>()/fonts.pixel_scale(),600,fonts.pixel_scale());realm::ui_rectangle(image,0,0,c.at("width").get<double>()/fonts.pixel_scale(),600,0x363636);realm::ui_blit(image,chooser.image(),0,0);
   realm::save_png(output/(c.at("key").get<std::string>()+".png"),image);metrics.push_back({{"key",c.at("key")},{"height",c.value("startup",false) ? 600 : chooser.view_height()}});
  }
  std::ofstream(output/"metrics.json")<<metrics.dump();
 }catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
