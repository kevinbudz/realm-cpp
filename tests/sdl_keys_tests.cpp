#include "realm/sdl_keys.hpp"
#include "realm/settings.hpp"
#include <iostream>
#include <stdexcept>
// SDL keys <-> the Flash key codes the Options screen stores (task 12-options).
namespace { int checks{};void check(bool ok,const std::string& why) { ++checks;if(!ok)throw std::runtime_error(why); } }
using namespace realm;
int main() {
    try {
        check(sdl_keys::flash_code(SDLK_a)==65&&sdl_keys::flash_code(SDLK_z)==90&&sdl_keys::flash_code(SDLK_0)==48&&sdl_keys::flash_code(SDLK_9)==57,"letters and digits");
        check(sdl_keys::flash_code(SDLK_F1)==112&&sdl_keys::flash_code(SDLK_F12)==123&&sdl_keys::flash_code(SDLK_F15)==126,"function keys");
        check(sdl_keys::flash_code(SDLK_RETURN)==13&&sdl_keys::flash_code(SDLK_KP_ENTER)==13&&sdl_keys::flash_code(SDLK_ESCAPE)==27&&sdl_keys::flash_code(SDLK_SPACE)==32,"enter, escape, space");
        check(sdl_keys::flash_code(SDLK_INSERT)==45&&sdl_keys::flash_code(SDLK_PAGEUP)==33&&sdl_keys::flash_code(SDLK_PAGEDOWN)==34&&sdl_keys::flash_code(SDLK_TAB)==9,"default hot keys");
        check(sdl_keys::flash_code(SDLK_SLASH)==191&&sdl_keys::flash_code(SDLK_EQUALS)==187&&sdl_keys::flash_code(SDLK_MINUS)==189&&sdl_keys::flash_code(SDLK_BACKQUOTE)==192,"punctuation");
        check(sdl_keys::flash_code(SDLK_KP_0)==96&&sdl_keys::flash_code(SDLK_KP_9)==105&&sdl_keys::flash_code(SDLK_KP_PLUS)==107,"numpad");
        check(sdl_keys::flash_code(SDLK_UNKNOWN)==0&&sdl_keys::flash_code(SDLK_VOLUMEUP)==0,"keys Flash cannot name are unset");
        // Every default binding names a key the SDL side can poll, and every named key round-trips.
        for(const auto& option:option_defs()) {
            if(option.kind!=OptionDef::Kind::key&&option.kind!=OptionDef::Kind::hidden_key)continue;
            if(option.fallback==0)continue;
            const auto sdl=sdl_keys::sdl_code(option.fallback);
            check(sdl!=SDLK_UNKNOWN&&sdl_keys::flash_code(sdl)==option.fallback,std::string("default binding of ")+std::string(option.param)+" round-trips");
        }
        for(int code=1;code<256;++code) {
            if(key_name(code).empty()||code==12||code==15||code==92||code==93||code==108)continue;
            const auto sdl=sdl_keys::sdl_code(code);
            check(sdl==SDLK_UNKNOWN||sdl_keys::flash_code(sdl)==code,"named key "+std::to_string(code)+" maps back to itself");
        }
        std::array<unsigned char,SDL_NUM_SCANCODES> keyboard{};
        check(!sdl_keys::held(keyboard.data(),87)&&!sdl_keys::held(keyboard.data(),0),"nothing held");
        keyboard[SDL_SCANCODE_W]=1;check(sdl_keys::held(keyboard.data(),87)&&!sdl_keys::held(keyboard.data(),83),"a held key matches its binding only");
        keyboard[SDL_SCANCODE_KP_ENTER]=1;check(sdl_keys::held(keyboard.data(),13),"either Enter key holds the Enter code");
        std::cout<<"SDL key checks: "<<checks<<'\n';
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
