#pragma once
#include <SDL_keyboard.h>
#include <SDL_keycode.h>
#include <array>
#include <utility>
namespace realm {
// Flash key codes (KeyboardEvent.keyCode, KeyCodes.as) for SDL keys. Settings keep the
// Flash codes; input arrives as SDL keycodes. Keys Flash has no code for map to 0.
namespace sdl_keys {
inline constexpr std::array<std::pair<SDL_Keycode,int>,50> table{{
    {SDLK_BACKSPACE,8},{SDLK_TAB,9},{SDLK_CLEAR,12},{SDLK_RETURN,13},{SDLK_KP_ENTER,13},
    {SDLK_LSHIFT,16},{SDLK_RSHIFT,16},{SDLK_LCTRL,17},{SDLK_RCTRL,17},{SDLK_LALT,18},{SDLK_RALT,18},
    {SDLK_PAUSE,19},{SDLK_CAPSLOCK,20},{SDLK_ESCAPE,27},{SDLK_SPACE,32},{SDLK_PAGEUP,33},{SDLK_PAGEDOWN,34},
    {SDLK_END,35},{SDLK_HOME,36},{SDLK_LEFT,37},{SDLK_UP,38},{SDLK_RIGHT,39},{SDLK_DOWN,40},
    {SDLK_INSERT,45},{SDLK_DELETE,46},{SDLK_LGUI,92},{SDLK_RGUI,92},{SDLK_APPLICATION,93},
    {SDLK_KP_MULTIPLY,106},{SDLK_KP_PLUS,107},{SDLK_KP_MINUS,109},{SDLK_KP_PERIOD,110},{SDLK_KP_DIVIDE,111},
    {SDLK_NUMLOCKCLEAR,144},{SDLK_SCROLLLOCK,145},{SDLK_SEMICOLON,186},{SDLK_EQUALS,187},{SDLK_COMMA,188},
    {SDLK_MINUS,189},{SDLK_PERIOD,190},{SDLK_SLASH,191},{SDLK_BACKQUOTE,192},{SDLK_LEFTBRACKET,219},
    {SDLK_BACKSLASH,220},{SDLK_RIGHTBRACKET,221},{SDLK_QUOTE,222},{SDLK_F13,124},{SDLK_F14,125},{SDLK_F15,126},
}};
// 0 when the key has no Flash key code.
[[nodiscard]] inline int flash_code(SDL_Keycode key) {
    if(key>=SDLK_a&&key<=SDLK_z)return 65+static_cast<int>(key-SDLK_a);
    if(key>=SDLK_0&&key<=SDLK_9)return 48+static_cast<int>(key-SDLK_0);
    if(key>=SDLK_F1&&key<=SDLK_F12)return 112+static_cast<int>(key-SDLK_F1);
    if(key>=SDLK_KP_1&&key<=SDLK_KP_9)return 97+static_cast<int>(key-SDLK_KP_1);
    if(key==SDLK_KP_0)return 96;
    for(const auto& [sdl,flash]:table)if(sdl==key)return flash;
    return 0;
}
// The key a Flash code names, for polling held keys. SDLK_UNKNOWN for an unset code.
[[nodiscard]] inline SDL_Keycode sdl_code(int code) {
    if(code>=65&&code<=90)return SDLK_a+(code-65);
    if(code>=48&&code<=57)return SDLK_0+(code-48);
    if(code>=112&&code<=123)return SDLK_F1+(code-112);
    if(code>=97&&code<=105)return SDLK_KP_1+(code-97);
    if(code==96)return SDLK_KP_0;
    // The first entry of each Flash code is the left/main key.
    for(const auto& [sdl,flash]:table)if(flash==code)return sdl;
    return SDLK_UNKNOWN;
}
[[nodiscard]] inline bool held(const unsigned char* keyboard,int code) {
    const auto key=sdl_code(code);
    if(key==SDLK_UNKNOWN)return false;
    // The Enter code is held by either Enter key.
    if(code==13)return keyboard[SDL_SCANCODE_RETURN]||keyboard[SDL_SCANCODE_KP_ENTER];
    if(code==16)return keyboard[SDL_SCANCODE_LSHIFT]||keyboard[SDL_SCANCODE_RSHIFT];
    if(code==17)return keyboard[SDL_SCANCODE_LCTRL]||keyboard[SDL_SCANCODE_RCTRL];
    if(code==18)return keyboard[SDL_SCANCODE_LALT]||keyboard[SDL_SCANCODE_RALT];
    return keyboard[SDL_GetScancodeFromKey(key)]!=0;
}
}
}
