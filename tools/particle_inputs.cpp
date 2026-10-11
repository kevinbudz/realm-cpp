#include "realm/scene.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) { try {
 if(argc!=3)throw std::runtime_error("Expected source states and output directory");
 using namespace realm;SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO)!=0)throw std::runtime_error(SDL_GetError());
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
 auto* window=SDL_CreateWindow("Native particle reference",0,0,1600,1200,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
 if(!window)throw std::runtime_error(SDL_GetError());
 auto context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK)throw std::runtime_error("GLEW");while(glGetError()!=GL_NO_ERROR){}
 {
 Catalog catalog(flash::source_root);AnimationLibrary animations(flash::source_root);Appearances appearances(flash::source_root,animations);Atlas atlas(flash::source_root,animations.images(),animations.images().size());Renderer renderer(16384);renderer.clear_color({54/255.F,54/255.F,54/255.F,1});Scene scene;Camera camera;World world(catalog);
 auto cases=nlohmann::json::parse(std::ifstream(argv[1]));const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
 std::size_t total{};
 for(const auto& c:cases) {
  world.initialize(100,100);std::vector<TextureDef> textures;textures.reserve(c.at("particles").size());
  for(const auto& state:c.at("particles")) {
   Particle p;p.position=p.start={state.at("x"),state.at("y")};p.z=state.at("z");p.size=state.at("size");p.color=state.value("color",0xffffffU);p.custom_size=state.value("custom",false);
   if(state.value("texture",false)) {
    TextureDef t;for(std::size_t i=0;i<flash::sheets.size();++i)if(flash::sheets[i].name==state.at("file").get<std::string>())t.sheet=static_cast<int>(i);t.index=state.at("index");textures.push_back(t);p.texture=&textures.back();p.bottom_anchor=state.value("bottom",false);
    if(c.at("type")==16) { p.motion=ParticleMotion::thrown_sprite;p.angle=state.value("rotation",.2); }
   }
   world.particles().emit(p);++total;
  }
  const double scale=c.at("scale");const int w=static_cast<int>(800*scale),h=static_cast<int>(600*scale);
  SDL_SetWindowSize(window,w,h);SDL_PumpEvents();SDL_GL_SwapWindow(window);camera.configure({50,50},0,w,h,scale,true);std::vector<Quad> quads;
  scene.build(world,camera,atlas,animations,nullptr,nullptr,&appearances,{50,50},0,scale,w,h,0,quads);renderer.draw(atlas,quads,w,h,w);save_png(out/(c.at("id").get<std::string>()+".png"),renderer.capture(w,h));
 }
 std::cout<<cases.size()<<" native Scene/OpenGL particle reference frames, "<<total<<" source particle states; GPU "<<glGetString(GL_RENDERER)<<"\n";
 }
 SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
 }catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
