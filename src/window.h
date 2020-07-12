#ifndef window_h
#define window_h

#include "common.h"
#include "SDL.h"

#define AXIS_DEADZONE 0x3fff

#define QUIT_REQUEST_DELAY 60

#define FRAME_DURATION 16

// Controller buttons
#define BUTTON_A 1
#define BUTTON_B (1 << 1)
#define BUTTON_SELECT (1 << 2)
#define BUTTON_START (1 << 3)
#define BUTTON_UP (1 << 4)
#define BUTTON_DOWN (1 << 5)
#define BUTTON_LEFT (1 << 6)
#define BUTTON_RIGHT (1 << 7)

// Forward declarations
typedef struct Driver Driver;

typedef struct Window {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Rect display_area;
    SDL_Rect mouse_area;
    SDL_Cursor *cursor;
    SDL_AudioDeviceID audio_id;
    SDL_GameController *js[2];
    bool js_use_axis[2];
    const int *buttons[2];
    bool fullscreen;
} Window;

int window_init(Window *wnd, Driver *driver, const char *filename);
void window_cleanup(Window *wnd);

void window_loop(Window *wnd, Driver *driver);

#endif /* window_h */
