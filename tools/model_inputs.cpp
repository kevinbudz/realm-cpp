#include "realm/model.hpp"
#include "realm/scene.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
// Native frames for the real-Flash model exporter. Items without an explicit rotation go
// through World/Scene (XML Rotation); items with one project the model directly.
int main(int argc,char** argv) { try {
 if(argc!=3)throw std::runtime_error("Expected source states and output directory");
 using namespace realm;SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO)!=0)throw std::runtime_error(SDL_GetError());
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
 auto* window=SDL_CreateWindow("Native model reference",0,0,1600,1200,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
 if(!window)throw std::runtime_error(SDL_GetError());
 auto context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK)throw std::runtime_error("GLEW");while(glGetError()!=GL_NO_ERROR){}
 {
 Catalog catalog(flash::source_root);AnimationLibrary animations(flash::source_root);Appearances appearances(flash::source_root,animations);Atlas atlas(flash::source_root,animations.images(),animations.images().size());Renderer renderer(16384);renderer.clear_color({54/255.F,54/255.F,54/255.F,1});Scene scene;Camera camera;World world(catalog);
 auto cases=nlohmann::json::parse(std::ifstream(argv[1]));const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
 std::size_t total{};
 for(const auto& c:cases) {
  world.initialize(100,100);const double scale=c.at("scale"),angle=c.at("angle");const int w=static_cast<int>(800*scale),h=static_cast<int>(600*scale);
  SDL_SetWindowSize(window,w,h);SDL_PumpEvents();SDL_GL_SwapWindow(window);camera.configure({50,50},angle,w,h,scale,true);std::vector<Quad> quads;
  bool direct=false;for(const auto& item:c.at("items"))direct|=item.contains("rotation");
  if(direct) {
   struct Placed { int depth;int id;std::shared_ptr<const MeshDraw> mesh; };std::vector<Placed> placed;int id=1;
   for(const auto& item:c.at("items")) {
    const auto* d=catalog.object(item.at("object").get<std::string>());if(!d)throw std::runtime_error("Unknown object "+item.at("object").get<std::string>());
    const Vec2 at{item.at("x"),item.at("y")};const auto& t=d->texture.select(0);std::optional<SpriteRegion> sprite;
    if(t.sheet>=0)sprite=atlas.sprite(static_cast<std::size_t>(t.sheet),t.index);
    const double rotation=item.contains("rotation")?item.at("rotation").get<double>():d->rotation;
    placed.push_back({static_cast<int>(camera.world_to_screen(at).y/scale+camera.clip_rect().y),id++,project_model(d->model,camera,at,rotation,d->color,sprite)});++total;
   }
   std::sort(placed.begin(),placed.end(),[](const auto& a,const auto& b){return a.depth==b.depth?a.id<b.id:a.depth<b.depth;});
   for(auto& p:placed)if(p.mesh)quads.push_back(mesh_quad(std::move(p.mesh)));
  } else {
   int wall_id=900;for(const auto& wall:c.value("walls",nlohmann::json::array())) {
    const auto* d=catalog.object(wall.at("object").get<std::string>());if(!d)throw std::runtime_error("Unknown wall object");
    world.add(wall_id++,d->type,{wall.at("x").get<double>()+.5,wall.at("y").get<double>()+.5});
   }
   int id=1;for(const auto& item:c.at("items")) { const auto* d=catalog.object(item.at("object").get<std::string>());if(!d)throw std::runtime_error("Unknown object");world.add(id++,d->type,{item.at("x"),item.at("y")});++total; }
   scene.build(world,camera,atlas,animations,nullptr,nullptr,&appearances,{50,50},angle,scale,w,h,0,quads);
  }
  // Flash draws ground shadows in a separate pass that this exporter omits; compare the models themselves.
  quads.erase(std::remove_if(quads.begin(),quads.end(),[](const Quad& q){return q.kind>.5F&&q.kind<1.5F;}),quads.end());
  renderer.draw(atlas,quads,w,h,w);save_png(out/(c.at("id").get<std::string>()+".png"),renderer.capture(w,h));
 }
 std::cout<<cases.size()<<" native Scene/OpenGL model reference frames, "<<total<<" model placements; GPU "<<glGetString(GL_RENDERER)<<"\n";
 }
 SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
 }catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
