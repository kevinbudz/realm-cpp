// Native counterparts of tools/flash-reference/StatusExporter.as: condition icons, status text and names
// drawn through the same classes the client uses, on the same canvases as the Flash export.
#include "realm/status_effects.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
namespace {
// Straight-alpha composite of a label (origin in logical pixels) onto a transparent canvas.
void place(realm::Image& canvas,const realm::StatusArt::Label& label,double x,double y) { realm::ui_blit(canvas,*label.image,x+label.offset.x,y+label.offset.y); }
}
int main(int argc,char** argv) {
 try {
  if(argc!=3)throw std::runtime_error("Expected tasks and output directory");
  const auto tasks=nlohmann::json::parse(std::ifstream(argv[1]));const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
  realm::AnimationLibrary animations(realm::flash::source_root);realm::Appearances appearances(realm::flash::source_root,animations);
  realm::FontBook fonts(tasks.at("fonts").get<std::string>());fonts.enable_native(realm::flash::source_root);
  realm::StatusArt art(fonts,&appearances);
  const auto metrics=nlohmann::json::parse(std::ifstream(tasks.at("flash_metrics").get<std::string>()));
  for(const auto& icon:metrics.at("icons"))
   realm::save_png(output/("icon-"+std::to_string(icon.at("id").get<int>())+"-"+std::to_string(icon.at("index").get<int>())+".png"),*art.icon(icon.at("frame")));
  for(const auto& text:tasks.at("texts")) {
   const auto label=art.status_text(text.at("text").get<std::string>(),text.at("color").get<std::uint32_t>());
   auto canvas=realm::ui_image(240,80,1);place(canvas,label,120-label.width/2,40-label.height/2);
   realm::save_png(output/("text-"+text.at("id").get<std::string>()+".png"),canvas);
  }
  for(const auto& name:tasks.at("names")) {
   const auto label=art.name(name.at("text").get<std::string>());
   auto canvas=realm::ui_image(std::ceil(label.width),64,1);place(canvas,label,0,0);
   realm::save_png(output/("name-"+name.at("id").get<std::string>()+".png"),canvas);
  }
 }catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
