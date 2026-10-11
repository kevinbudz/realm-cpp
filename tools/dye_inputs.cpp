#include "realm/appearance.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
 try {
  if(argc!=4)throw std::runtime_error("Expected cases, Flash output and native output");
  realm::AnimationLibrary animations(realm::flash::source_root);realm::Appearances appearances(realm::flash::source_root,animations);
  const auto cases=nlohmann::json::parse(std::ifstream(argv[1]));const std::filesystem::path source=argv[2],output=argv[3];std::filesystem::create_directories(output);nlohmann::json metrics=nlohmann::json::array();
  for(const auto& c:cases) {
   const auto key=c.at("key").get<std::string>();const auto raw=realm::load_png(source/(key+"-source.png"));realm::Image mask;
   if(std::filesystem::exists(source/(key+"-mask.png")))mask=realm::load_png(source/(key+"-mask.png"));
   auto image=appearances.compose(raw,mask.pixels.empty()?nullptr:&mask,c.at("size"),c.at("pad"),c.at("tex1"),c.at("tex2"));
   realm::save_png(output/(key+".png"),image);metrics.push_back({{"key",key},{"width",image.width},{"height",image.height}});
  }
  std::ofstream(output/"metrics.json")<<metrics.dump();
 }catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
