#include "window.h"
#include "ppu.h"
#include "memory_maps.h"

static int identify_js(Window *wnd, SDL_JoystickID which) {
    for (int i = 0; i < 2; i++) {
        if (wnd->js[i]) {
            if (SDL_JoystickInstanceID(wnd->js[i]) == which) {
                return i;
            }
        }
    }
    return -1;
}

// PUBLIC FUNCTIONS //

int window_init(Window *wnd, const char *filename) {
    // Init SDL
    int error_code = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
    if (error_code) {
        printf("%s\n", SDL_GetError());
        return error_code;
    }
    
    // Attempt to open up to 2 controllers
    wnd->js_use_axis[0] = wnd->js_use_axis[1] = false;
    wnd->js[0] = wnd->js[1] = NULL;
    int n_js = SDL_NumJoysticks();
    if (n_js < 0) {
        printf("%s\n", SDL_GetError());
    }
    int assigned_js = 0;
    for (int i = 0; i < n_js; i++) {
        SDL_Joystick *js = SDL_JoystickOpen(i);
        if (js) {
            wnd->js[assigned_js++] = js;
            printf("Assigned \"%s\" as controller #%d\n",
                   SDL_JoystickName(js), assigned_js);
            if (assigned_js >= 2) {
                break;
            }
        } else {
            printf("%s\n", SDL_GetError());
        }
    }
    if (!assigned_js) {
        printf("No controllers were found, will continue without input\n");
    }
    
    Uint32 window_flags = SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef DEBUG
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
#else
    window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
    
    // Create window and renderer
    wnd->window = SDL_CreateWindow(filename, SDL_WINDOWPOS_UNDEFINED,
                                             SDL_WINDOWPOS_UNDEFINED,
                                             WIDTH_ADJUSTED, HEIGHT_CROPPED,
                                             window_flags);
    if (!wnd->window) {
        printf("%s\n", SDL_GetError());
        return 1;
    }
    wnd->renderer = SDL_CreateRenderer(wnd->window, -1, 0);
    if (!wnd->renderer) {
        printf("%s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(wnd->renderer, WIDTH_ADJUSTED, HEIGHT_CROPPED);
    wnd->texture = SDL_CreateTexture(wnd->renderer, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     WIDTH, HEIGHT);
    if (!wnd->texture) {
        printf("%s\n", SDL_GetError());
        return 1;
    }
    
    SDL_ShowCursor(SDL_DISABLE);

    return 0;
}

void window_cleanup(Window *wnd) {
    SDL_DestroyTexture(wnd->texture);
    SDL_DestroyRenderer(wnd->renderer);
    SDL_DestroyWindow(wnd->window);
    
    for (int i = 0; i < 2; i++) {
        if (wnd->js[i]) {
            SDL_JoystickClose(wnd->js[i]);
        }
    }

    SDL_Quit();
}

void window_update_screen(Window *wnd, const PPUState *ppu) {
    static const SDL_Rect screen_visible_area =
        {0, (HEIGHT - HEIGHT_CROPPED) / 2, WIDTH, HEIGHT_CROPPED};
    SDL_UpdateTexture(wnd->texture, NULL, ppu->screen,
                      WIDTH * sizeof(uint32_t));
    SDL_RenderClear(wnd->renderer);
    SDL_RenderCopy(wnd->renderer, wnd->texture, &screen_visible_area, NULL);
    SDL_RenderPresent(wnd->renderer);
}

bool window_process_events(Window *wnd, uint8_t *controllers) {
    // Button assignments
    // Hardcoded to PS4 controller (and 8bitdo's "macOS mode") for now
    // A, B, Select, Start, Up, Down, Left, Right
    static const int buttons[] = {1, 0, 4, 6, 11, 12, 13, 14};
    
    SDL_Event event;
    int cid;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_JOYAXISMOTION:
                if (event.jaxis.axis >= 2) {
                    break;
                }
                cid = identify_js(wnd, event.jaxis.which);
                if (cid < 0) {
                    break;
                }
                if (!wnd->js_use_axis[cid]) {
                    if (abs(event.jaxis.value) < AXIS_DEADZONE) {
                        break;
                    }
                    controllers[cid] &= 0b1111;
                    wnd->js_use_axis[cid] = true;
                }
                if (event.jaxis.axis == 0) {
                    controllers[cid] &= ~(BUTTON_LEFT | BUTTON_RIGHT);
                    if (event.jaxis.value < -AXIS_DEADZONE) {
                        controllers[cid] |= BUTTON_LEFT;
                    } else if (event.jaxis.value > AXIS_DEADZONE) {
                        controllers[cid] |= BUTTON_RIGHT;
                    }
                } else if (event.jaxis.axis == 1) {
                    controllers[cid] &= ~(BUTTON_UP | BUTTON_DOWN);
                    if (event.jaxis.value < -AXIS_DEADZONE) {
                        controllers[cid] |= BUTTON_UP;
                    } else if (event.jaxis.value > AXIS_DEADZONE) {
                        controllers[cid] |= BUTTON_DOWN;
                    }
                }
                /*printf("P%d A%d:%d => %d\n", cid + 1,
                       event.jaxis.axis, event.jaxis.value,
                       controllers[cid]);*/
                break;
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
                cid = identify_js(wnd, event.jbutton.which);
                if (cid < 0) {
                    break;
                }
                for (int i = 0; i < 8; i++) {
                    if (event.jbutton.button == buttons[i]) {
                        if (i > 3 && wnd->js_use_axis[cid]) {
                            controllers[cid] &= 0b1111;
                            wnd->js_use_axis[cid] = false;
                        }
                        if (event.jbutton.state == SDL_PRESSED) {
                            controllers[cid] |= 1 << i;
                        } else {
                            controllers[cid] &= ~(1 << i);
                        }
                        break;
                    }
                }
                /*printf("P%d B%d:%d => %d\n", cid + 1,
                       event.jbutton.button, event.jbutton.state,
                       controllers[cid]);*/
                break;
            case SDL_QUIT:
                return true;
        }
    }
    return false;
}
