#ifndef window_h
#define window_h

#include "common.h"
#include "SDL.h"

// Forward declarations
typedef struct PPUState PPUState;

typedef struct Window {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_JoystickID js[2];
} Window;

int window_init(Window *wnd);
void window_cleanup(Window *wnd);

void window_update_screen(Window *wnd, const PPUState *ppu);
bool window_process_events(Window *wnd, uint8_t *controllers);

#endif /* window_h */
