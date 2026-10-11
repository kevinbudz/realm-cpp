#define SDL_MAIN_HANDLED
#include "realm/hud.hpp"
#include "hud_fixture.hpp"
#include "realm/renderer.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    SDL_Window* window{};SDL_GLContext context{};
    try {
        if(argc!=3)throw std::runtime_error("Usage: realm_hud_gpu_inputs FONT_DIRECTORY TASKS.json");
        SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO)!=0)throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        window=SDL_CreateWindow("HUD reference test",0,0,200,600,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);if(!window)throw std::runtime_error(SDL_GetError());
        context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK)throw std::runtime_error("GLEW initialization failed");while(glGetError()!=GL_NO_ERROR) {}
        {
            std::ifstream input(argv[2]);const auto tasks=nlohmann::json::parse(input);const auto output=std::filesystem::path(argv[2]).parent_path()/"native-gpu";
            realm::FontBook fonts(argv[1]);realm::Catalog catalog(realm::flash::source_root);
            std::unique_ptr<realm::HudArt> art;if(tasks.contains("art"))art=std::make_unique<realm::HudArt>(tasks.at("art").get<std::string>());
            realm::Hud hud(fonts,art.get());const std::array images{hud.image()};realm::Atlas atlas(realm::flash::source_root,images);realm::Renderer renderer(2);
            realm::Quad world;world.kind=2;world.axis_x={200,0};world.axis_y={0,600};const std::array world_quads{world};
            auto overlay=realm::sprite_quad(atlas.additional(0),{0,0},{200,0},{0,600});overlay.kind=5;const std::array overlays{overlay};
            for(const auto& task:tasks.at("cases")) {
                auto state=hud_fixture(task,catalog);
                hud.update(state);for(const auto& region:hud.changes())atlas.update_additional_region(0,hud.image(),region.x,region.y,region.width,region.height);
                renderer.draw(atlas,world_quads,200,600,0,overlays);realm::save_png(output/(task.at("id").get<std::string>()+".png"),renderer.capture(200,600));
                if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("HUD upload/render OpenGL error");
            }
            std::cout<<tasks.at("cases").size()<<" panels rendered through incremental atlas uploads and GPU overlay\n";
        }
        SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    }catch(const std::exception& error) { if(context)SDL_GL_DeleteContext(context);if(window)SDL_DestroyWindow(window);SDL_Quit();std::cerr<<error.what()<<'\n';return 1; }
}
