#include "realm/party_arrows.hpp"
#include "realm/player_group.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
try {
 if(argc!=5)throw std::runtime_error("Expected fonts, art, cases, output");
 using namespace realm;
 Catalog catalog(flash::source_root);FontBook fonts(argv[1]);fonts.enable_native(flash::source_root);HudArt art(argv[2]);PlayerGroup group(catalog,fonts,art);
 std::ifstream input(argv[3]);auto cases=nlohmann::json::parse(input);std::filesystem::path out(argv[4]);std::filesystem::create_directories(out);nlohmann::json metrics=nlohmann::json::array();
 for(const auto& c:cases) {
  const int w=c.at("width"),h=c.at("height");const double zoom=c.at("zoom");Camera camera;camera.configure({50,50},c.at("angle"),w,h,zoom,c.at("centered"));
  const auto a=PartyArrows::project(camera,{c.at("x"),c.at("y")});if(!a)throw std::runtime_error("Expected arrow");
  auto image=ui_image(w,h,1);auto arrow=PartyArrows::image(a->rotation,c.at("hover"),std::max(1.0,zoom));
  // Composite at physical zoom; compare the entire playing fixture.
  arrow.pixel_scale/=zoom;ui_blit(image,arrow,a->position.x-24*zoom,a->position.y-24*zoom,false,true);
  if(c.at("tooltip")) {
   fonts.set_pixel_scale(h/600.0*zoom);std::vector<PlayerMenuTarget> rows;
   for(const auto& row:c.at("players")) { PlayerMenuTarget r;r.type=row.at("type");r.name=row.at("name");r.level=row.at("level");rows.push_back(r); }
   group.close();group.arrow_tooltip(rows,a->position,a->rotation,zoom,h/600.0*zoom);const auto pos=group.canvas_position();auto tip=group.image();tip.pixel_scale/=h/600.0*zoom;ui_blit(image,tip,pos.x,pos.y,false,true);
  }
  // ui_blit's destination is premultiplied, PNG is straight.
  for(std::size_t i=0;i<image.pixels.size();i+=4)if(image.pixels[i+3])for(int j=0;j<3;++j)image.pixels[i+j]=static_cast<unsigned char>(std::min(255U,static_cast<unsigned>(image.pixels[i+j])*255/image.pixels[i+3]));
  const auto id=c.at("id").get<std::string>();save_png(out/(id+".png"),image);metrics.push_back({{"id",id},{"x",a->position.x},{"y",a->position.y},{"rotation",a->rotation*180/std::acos(-1.0)}});
 }
 std::ofstream(out/"metrics.json")<<metrics.dump(2);
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
