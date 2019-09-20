#ifndef window_h
#define window_h

#include "common.h"
#include "SDL.h"

#define AXIS_DEADZONE 0x3fff

#define QUIT_REQUEST_DELAY 60

#define FRAME_DURATION 16

// Forward declarations
typedef struct Machine Machine;
typedef struct PPUState PPUState;

typedef struct Window {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Rect display_area;
    SDL_Rect mouse_area;
    SDL_Cursor *cursor;
    SDL_Joystick *js[2];
    bool js_use_axis[2];
} Window;

int window_init(Window *wnd, const char *filename);
void window_cleanup(Window *wnd);

void window_loop(Window *wnd, Machine *vm);

#endif /* window_h */
