#include "realm/renderer.hpp"
#include "realm/atlas.hpp"
#include "realm/backdrop.hpp"
#include "realm/camera.hpp"
#include "realm/image.hpp"
#include "realm/post_effects.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string_view>
// Native frames for the real-Flash PostFxExporter. The source frame is drawn as the world through
// the real Renderer with the case's effects, so the comparison covers the GPU filter and overlay path.
int main(int argc,char** argv) { try {
 if(argc==3&&std::string_view(argv[1])=="--water") { realm::save_png(argv[2],realm::nexus_water(12345U));return 0; }
 if(argc!=3)throw std::runtime_error("Expected source states and output directory");
 using namespace realm;SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO)!=0)throw std::runtime_error(SDL_GetError());
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
 auto* window=SDL_CreateWindow("Native post effect reference",0,0,800,600,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
 if(!window)throw std::runtime_error(SDL_GetError());
 auto context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK)throw std::runtime_error("GLEW");while(glGetError()!=GL_NO_ERROR){}
 {
 Atlas atlas(flash::source_root);Renderer renderer(16);
 const auto cases=nlohmann::json::parse(std::ifstream(argv[1]));const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
 for(const auto& c:cases) {
  if(c.contains("backdrop")) {
   // Background layer alone on black, 800x600 game camera centered on the player, as in Map.as.
   const auto kind=c.at("backdrop").get<std::string>();
   Camera camera;camera.configure({50,50},c.at("angle").get<double>(),800,600,1,true);
   Backdrop backdrop;backdrop.reset(kind=="stars" ? 1 : 2,c.value("seed",12345U));
   if(kind=="stars") {
    std::vector<Star> stars;for(const auto& s:c.at("stars"))stars.push_back({s.at("x").get<double>(),s.at("y").get<double>(),s.at("scale").get<double>(),s.at("image").get<int>()});
    backdrop.set_stars(std::move(stars));
   } else if(kind=="nexus-replica") backdrop.set_water(load_png(c.at("source").get<std::string>()));
   std::vector<Quad> quads;backdrop.build(camera,atlas,c.at("time").get<std::int32_t>(),quads);
   renderer.draw(atlas,quads,800,600,800);
   save_png(out/(c.at("id").get<std::string>()+".png"),renderer.capture(800,600));
   continue;
  }
  const auto source=load_png(c.at("source").get<std::string>());
  SurfaceTexture texture;texture.update(source);
  const int clip=c.at("clip_width").get<int>(),height=c.at("height").get<int>();
  Quad world=texture.quad({0,0},{static_cast<double>(source.width),0},{0,static_cast<double>(source.height)});world.kind=6;
  ScreenEffects effects;effects.blind=c.at("blind").get<bool>();effects.blur=c.at("blur").get<float>();effects.vignette=c.at("vignette").get<float>();
  effects.sidebar=c.at("sidebar").get<bool>() ? sidebar_gradient_width : 0;
  renderer.draw(atlas,std::span<const Quad>(&world,1),800,600,clip,{},effects);
  save_png(out/(c.at("id").get<std::string>()+".png"),renderer.capture(800,600));
  if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("GL error in "+c.at("id").get<std::string>());
  (void)height;
 }
 }
 SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
