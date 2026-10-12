#include <GL/glew.h>
#include "realm/startup.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <iostream>

int main(int argc,char** argv) {
    SDL_Window* window{};SDL_GLContext context{};
    try {
        if(argc!=5)throw std::runtime_error("Usage: realm_startup_resize_tests FONT_DIRECTORY ART_DIRECTORY HTTP_PORT OUTPUT_DIRECTORY");
        SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        window=SDL_CreateWindow("Pulse resize checks",0,0,1600,1200,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        if(!window)throw std::runtime_error(SDL_GetError());
        context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());
        glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK)throw std::runtime_error("GLEW initialization failed");
        while(glGetError()!=GL_NO_ERROR) {}
        {
            using namespace realm;
            FontBook fonts(argv[1]);fonts.enable_native(flash::source_root);HudArt art(argv[2]);Catalog catalog(flash::source_root);
            const std::filesystem::path output=argv[4];
            StartupOptions options;options.app_port=static_cast<std::uint16_t>(std::stoi(argv[3]));options.account_file=output/"unused-fixture-account.json";
            Startup startup(fonts,&art,catalog,flash::source_root,options);
            SurfaceTexture sheet;Atlas atlas(flash::source_root);Renderer renderer(512);std::vector<Quad> quads;
            auto update=[&](int time,int width,int height) {
                fonts.set_pixel_scale(height/600.0);startup.update(time,0,width,height);
                for(const auto& area:startup.uploads())sheet.update_region(startup.sheet(),area.x,area.y,area.width,area.height);
                startup.clear_uploads();quads.clear();startup.build(sheet,time,quads);
            };
            // The Python runner supplies the guest char-list HTTP fixture.
            bool ready{};
            for(int attempt=0;attempt<2000&&!ready;++attempt) {
                update(0,800,600);
                const auto sheet_texture=sheet.quad({}, {}, {}).texture;
                ready=std::any_of(quads.begin(),quads.end(),[&](const Quad& q){return q.texture&&q.texture!=sheet_texture;});
                if(!ready)SDL_Delay(1);
            }
            if(!ready)throw std::runtime_error("Title screen did not finish loading");
            std::vector<double> times;
            std::size_t checks{};
            for(const auto size:std::array<Vec2,4>{{{800,600},{1280,720},{1600,1200},{3840,2160}}}) {
                GLuint pulse_texture{};GLint storage_width{},storage_height{};
                for(int frame=0;frame<180;++frame) {
                    const auto begin=std::chrono::steady_clock::now();
                    update(frame*16,static_cast<int>(size.x),static_cast<int>(size.y));
                    const auto sheet_texture=sheet.quad({}, {}, {}).texture;
                    bool found{};
                    for(const auto& q:quads)if(q.texture&&q.texture!=sheet_texture) {
                        found=true;glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,q.texture);
                        GLint w{},h{};glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
                        if(std::abs(q.axis_x[0]-q.uv[2]*static_cast<float>(w))>.001F||std::abs(q.axis_y[1]-q.uv[3]*static_cast<float>(h))>.001F)
                            throw std::runtime_error("Pulse stretches a bitmap instead of displaying current-size glyphs");
                        if(frame==0) { pulse_texture=q.texture;storage_width=w;storage_height=h; }
                        if(q.texture!=pulse_texture||w!=storage_width||h!=storage_height)throw std::runtime_error("Pulse reallocates GPU storage mid-cycle");
                        ++checks;
                    }
                    if(!found)throw std::runtime_error("Play pulse is missing");
                    glActiveTexture(GL_TEXTURE0);
                    times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
                    if(size.x==1600&&frame%30==0) {
                        renderer.draw(atlas,{},1600,1200,1600,quads);
                        save_png(output/("pulse-"+std::to_string(frame)+".png"),renderer.capture(1600,1200));
                    }
                    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("OpenGL error during pulse checks");
                }
            }
            std::sort(times.begin(),times.end());
            std::cout<<checks<<" pulse frames preserve native texel size and reuse GPU storage; isolated menu build/upload median="
                     <<times[times.size()/2]<<"ms p95="<<times[times.size()*95/100]<<"ms\n";
        }
        SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return 0;
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';if(context)SDL_GL_DeleteContext(context);if(window)SDL_DestroyWindow(window);SDL_Quit();return 1;
    }
}
