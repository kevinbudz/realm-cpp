#include "realm/appearance.hpp"
#include "realm/player_group.hpp"
#include "realm/player_tooltip.hpp"
#include "flash_assets.hpp"
#include <iostream>
#include <stdexcept>
namespace { int checks{};void check(bool value,const char* message) { ++checks;if(!value)throw std::runtime_error(message); } }
int main(int argc,char** argv) { try {
 using namespace realm;Catalog catalog(flash::source_root);AnimationLibrary animations(flash::source_root);Appearances appearances(flash::source_root,animations);
 const auto& def=catalog.player_texture(782,0);const auto* c=animations.character(def.animated,def.index);check(c!=nullptr,"Wizard animation");
 const auto frame=c->directions[0][0][0];check(frame.mask>=0,"Player mask retained");
 auto original=appearances.animated(frame,100,false,0,0);auto clothing=appearances.animated(frame,100,false,0x1ff0000,0);auto accessory=appearances.animated(frame,100,false,0,0x100ff00);
 check(original->pixels!=clothing->pixels,"Clothing dye applied");check(original->pixels!=accessory->pixels,"Accessory dye applied");check(clothing!=accessory,"Distinct dye keys");check(clothing==appearances.animated(frame,100,false,0x1ff0000,0),"Lazy hit retains bitmap");
 for(const int kind:{4,5,9,10})check(appearances.animated(frame,137,false,static_cast<std::uint32_t>(kind<<24)|7,0)->pixels!=original->pixels,"All textile sets supported");
 const auto* item=catalog.object(0x1000);check(item&&item->texture.mask_sheet>=0,"Static Mask imported");check(appearances.texture(item->texture,0,100,false,0x1ff0000,0)->pixels!=appearances.texture(item->texture,0,100,false,0,0)->pixels,"Object mask coloring");
 const auto* no_mask=animations.character(0,0);check(no_mask!=nullptr,"Unmasked character");auto unmasked=no_mask->directions[0][0][0];check(appearances.animated(unmasked,123,false,0,0)->pixels==appearances.animated(unmasked,123,false,0x1ff0000,0)->pixels,"Dyes leave unmasked artwork intact");
 World world(catalog);world.initialize(100,100,"Dye fixture");world.set_player_id(1);world.add(1,782,{50,50});world.add(2,782,{51,50});
 const std::array<std::int32_t,1> ids{2};auto rows=player_group_targets(world,ids);auto before=player_tooltip_target(world,2);const auto state=HudState::player(world,*world.entity(1));
 net::Tick tick;net::Status status;status.id=2;status.position={51,50};status.stats={{29,static_cast<std::int32_t>(0x1ff0000)},{30,static_cast<std::int32_t>(0x5000008)},{70,836}};tick.statuses.push_back(status);world.apply(tick);
 auto after=player_tooltip_target(world,2);check(before&&after&&*before!=*after,"Tooltip follows live dyes");check(after->tex1==0x1ff0000&&after->tex2==0x5000008&&after->skin==836,"All appearance stats authoritative");check(rows[0].tex1==0&&rows[0].skin==0,"Row snapshot owns original data");check(player_group_targets(world,ids)[0].tex2==0x5000008,"Groups expose current dye");
 world.entity(1)->tex1=0x1ff0000;check(state.inventory!=HudState::player(world,*world.entity(1)).inventory,"HUD heading detects dye");
 const auto* merchant=catalog.object("Merchant");check(merchant!=nullptr,"Merchant definition");
 auto& shop=world.add(3,merchant->type,{50.2,50.2});shop.stats[31]=0x1000;
 check(world_dyes(world,*world.entity(1))[0]==0x1f0f8ff,"Nearby dye merchant world preview");check(world.entity(1)->tex1==0x1ff0000,"Merchant preview never mutates authoritative dye");
 check(world_dyes(world,*world.entity(2))[0]==world.entity(2)->tex1,"Remote player has no local merchant preview");
 world.entity(3)->position={55,55};check(world_dyes(world,*world.entity(1))[0]==world.entity(1)->tex1,"Leaving merchant restores dye");
 int sizes=0,definitions=0;
 for(const auto& d:catalog.objects())if(d.texture.sheet>=0||d.texture.animated>=0||!d.texture.random.empty()) {
  // One representative per definition, including min/max and stepped sizes.
  const int size=d.min_size+(d.max_size-d.min_size)/2;auto image=appearances.texture(d.texture,static_cast<std::uint32_t>(d.type),size,false,static_cast<std::uint32_t>(d.xml.number("Tex1")),static_cast<std::uint32_t>(d.xml.number("Tex2")));
  check(!image->pixels.empty()&&image->width<=1048&&image->height<=1048,"Every catalog appearance has a bounded bitmap");++definitions;
 }
 for(const int size:{1,5,17,50,73,100,137,200,350,500,1000,10000,65535}) { auto image=appearances.animated(frame,size,false,0x1ff0000,0);check(image->width<=1048&&image->height<=1048,"Live size raster bounded");++sizes; }
 if(argc>1) {
  HudArt art(argv[1]);art.appearances(&appearances,&catalog);
  check(art.contains("item4866"),"Unbaked textile icon available");check(!art.image("item4866").pixels.empty(),"Unbaked textile icon generated");
  int requested=0;for(const auto& d:catalog.objects())if(d.class_name=="Dye") {
   (void)art.image("item"+std::to_string(d.type));if(++requested>=200)break;
  }
  check(requested==200&&art.cache_entries()<=256&&art.cache_bytes()<=8*1024*1024,"Dynamic icons obey HUD cache bounds");
 }
 auto retained=appearances.animated(frame,100,false,0x1ff0000,0);const auto saved=retained->pixels;
 for(unsigned i=1;i<=Appearances::maximum_entries+256;++i)(void)appearances.animated(frame,50,false,0x1000000|i,0);
 check(appearances.entries()<=Appearances::maximum_entries&&appearances.bytes()<=Appearances::maximum_bytes,"Count and byte cache bounds");check(appearances.evictions()>0,"Churn evicts");check(retained->pixels==saved,"Eviction cannot invalidate retained draw");
 appearances.clear();check(appearances.entries()==0&&appearances.bytes()==0,"Map teardown clears compositions");
 for(unsigned i=0;i<48;++i)(void)appearances.animated(frame,3000,false,0x1000000|i,0);
 check(appearances.bytes()<=Appearances::maximum_bytes&&appearances.entries()<48,"Byte bound evicts below count bound");
 std::cout<<checks<<" dye/mask/cache/stat checks passed, "<<definitions<<" catalog definitions and "<<sizes<<" live sizes\n";
 }catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
