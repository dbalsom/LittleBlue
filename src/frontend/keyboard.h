#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_keycode.h>

inline uint8_t translate_SDL_key(SDL_Keycode code, bool pressed) {
    uint8_t sc = 0;
    switch (code) {
        // Letters
        case SDLK_A: sc = 0x1E; break;
        case SDLK_B: sc = 0x30; break;
        case SDLK_C: sc = 0x2E; break;
        case SDLK_D: sc = 0x20; break;
        case SDLK_E: sc = 0x12; break;
        case SDLK_F: sc = 0x21; break;
        case SDLK_G: sc = 0x22; break;
        case SDLK_H: sc = 0x23; break;
        case SDLK_I: sc = 0x17; break;
        case SDLK_J: sc = 0x24; break;
        case SDLK_K: sc = 0x25; break;
        case SDLK_L: sc = 0x26; break;
        case SDLK_M: sc = 0x32; break;
        case SDLK_N: sc = 0x31; break;
        case SDLK_O: sc = 0x18; break;
        case SDLK_P: sc = 0x19; break;
        case SDLK_Q: sc = 0x10; break;
        case SDLK_R: sc = 0x13; break;
        case SDLK_S: sc = 0x1F; break;
        case SDLK_T: sc = 0x14; break;
        case SDLK_U: sc = 0x16; break;
        case SDLK_V: sc = 0x2F; break;
        case SDLK_W: sc = 0x11; break;
        case SDLK_X: sc = 0x2D; break;
        case SDLK_Y: sc = 0x15; break;
        case SDLK_Z: sc = 0x2C; break;

        // Numbers (top row)
        case SDLK_1: sc = 0x02; break;
        case SDLK_2: sc = 0x03; break;
        case SDLK_3: sc = 0x04; break;
        case SDLK_4: sc = 0x05; break;
        case SDLK_5: sc = 0x06; break;
        case SDLK_6: sc = 0x07; break;
        case SDLK_7: sc = 0x08; break;
        case SDLK_8: sc = 0x09; break;
        case SDLK_9: sc = 0x0A; break;
        case SDLK_0: sc = 0x0B; break;

        // Enter, esc, backspace, tab, space
        case SDLK_RETURN: sc = 0x1C; break;
        case SDLK_ESCAPE: sc = 0x01; break;
        case SDLK_BACKSPACE: sc = 0x0E; break;
        case SDLK_TAB: sc = 0x0F; break;
        case SDLK_SPACE: sc = 0x39; break;

        // Punctuation
        case SDLK_MINUS: sc = 0x0C; break;       // -
        case SDLK_EQUALS: sc = 0x0D; break;      // =
        case SDLK_LEFTBRACKET: sc = 0x1A; break; // [
        case SDLK_RIGHTBRACKET: sc = 0x1B; break;// ]
        case SDLK_BACKSLASH: sc = 0x2B; break;   // \
        case SDLK_SEMICOLON: sc = 0x27; break;   // ;
        case SDLK_APOSTROPHE: sc = 0x28; break;       // '
        case SDLK_COMMA: sc = 0x33; break;       // ,
        case SDLK_PERIOD: sc = 0x34; break;      // .
        case SDLK_SLASH: sc = 0x35; break;       // /
        case SDLK_GRAVE: sc = 0x29; break;       // `

        // Modifier keys
        case SDLK_LSHIFT: sc = 0x2A; break;
        case SDLK_RSHIFT: sc = 0x36; break;
        case SDLK_LCTRL: sc = 0x1D; break;
        case SDLK_RCTRL: sc = 0x1D; break; // right ctrl is E0 1D in extended; return base 1D
        case SDLK_LALT: sc = 0x38; break;
        case SDLK_RALT: sc = 0x38; break;  // right alt is E0 38 extended in some sets
        case SDLK_CAPSLOCK: sc = 0x3A; break;

        // Function keys
        case SDLK_F1: sc = 0x3B; break;
        case SDLK_F2: sc = 0x3C; break;
        case SDLK_F3: sc = 0x3D; break;
        case SDLK_F4: sc = 0x3E; break;
        case SDLK_F5: sc = 0x3F; break;
        case SDLK_F6: sc = 0x40; break;
        case SDLK_F7: sc = 0x41; break;
        case SDLK_F8: sc = 0x42; break;
        case SDLK_F9: sc = 0x43; break;
        case SDLK_F10: sc = 0x44; break;
        case SDLK_F11: sc = 0x57; break;
        case SDLK_F12: sc = 0x58; break;

        // Arrow keys (use set1 codes; note some are E0-prefixed in extended sets)
        case SDLK_UP: sc = 0x48; break;
        case SDLK_DOWN: sc = 0x50; break;
        case SDLK_LEFT: sc = 0x4B; break;
        case SDLK_RIGHT: sc = 0x4D; break;

        // Insert/Delete/Home/End/PageUp/PageDown
        case SDLK_INSERT: sc = 0x52; break; // often E0 52
        case SDLK_DELETE: sc = 0x53; break; // often E0 53
        case SDLK_HOME: sc = 0x47; break;
        case SDLK_END: sc = 0x4F; break;
        case SDLK_PAGEUP: sc = 0x49; break;
        case SDLK_PAGEDOWN: sc = 0x51; break;

        // Numpad
        case SDLK_KP_1: sc = 0x4F; break;
        case SDLK_KP_2: sc = 0x50; break;
        case SDLK_KP_3: sc = 0x51; break;
        case SDLK_KP_4: sc = 0x4B; break;
        case SDLK_KP_5: sc = 0x4C; break;
        case SDLK_KP_6: sc = 0x4D; break;
        case SDLK_KP_7: sc = 0x47; break;
        case SDLK_KP_8: sc = 0x48; break;
        case SDLK_KP_9: sc = 0x49; break;
        case SDLK_KP_0: sc = 0x52; break;

        // Numpad keys (map to their usual set1 codes)
        case SDLK_KP_PLUS: sc = 0x4E; break;
        case SDLK_KP_MINUS: sc = 0x4A; break;
        case SDLK_KP_PERIOD: sc = 0x53; break;
        case SDLK_KP_ENTER: sc = 0x1C; break; // often E0 1C
        case SDLK_KP_DIVIDE: sc = 0x35; break; // may be E0-prefixed
        case SDLK_KP_MULTIPLY: sc = 0x37; break;
        case SDLK_KP_EQUALS: sc = 0x0D; break;




        default:
            sc = 0;
            break;
    }

    if (sc != 0 && !pressed) sc |= 0x80; // set high bit for key-up
    return sc;
}