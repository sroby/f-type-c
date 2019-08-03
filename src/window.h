#ifndef window_h
#define window_h

#include "common.h"
#include "SDL.h"

#include "ppu.h"

#define AXIS_DEADZONE 0x3fff

#define QUIT_REQUEST_DELAY 1000
#define FRAME_DURATION 16

// Forward declarations
typedef struct Machine Machine;

typedef struct Window {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Rect display_area;
    SDL_Joystick *js[2];
    bool js_use_axis[2];
    Machine *vm;
    SDL_mutex *mutex;
    SDL_atomic_t quitting;
    uint32_t screen_buffer[WIDTH * HEIGHT];
} Window;

int window_init(Window *wnd, const char *filename, Machine *vm);
void window_cleanup(Window *wnd);

void window_loop(Window *wnd);

#endif /* window_h */
