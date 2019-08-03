#include "window.h"

#include "machine.h"
#include "memory_maps.h"

// Button assignments
// Hardcoded to PS4 controller (and 8bitdo's "macOS mode") for now
// A, B, Select, Start, Up, Down, Left, Right
static const int buttons[] = {1, 0, 4, 6, 11, 12, 13, 14};

static const SDL_Rect screen_visible_area =
    {0, (HEIGHT - HEIGHT_CROPPED) / 2, WIDTH, HEIGHT_CROPPED};

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

static int thread_machine_loop(void *data) {
    Window *wnd = data;
    const char *const verb_char = getenv("VERBOSE");
    const bool verbose = verb_char ? *verb_char - '0' : false;
    int frame = 0;
    
    uint32_t t_next = 0;
    while (!SDL_AtomicGet(&wnd->quitting)) {
        // Advance one frame
        if (!machine_advance_frame(wnd->vm, verbose)) {
            SDL_AtomicSet(&wnd->quitting, 1);
            break;
        }
        SDL_LockMutex(wnd->mutex);
        memcpy(wnd->screen_buffer, wnd->vm->ppu->screen,
               sizeof(uint32_t) * WIDTH * HEIGHT);
        SDL_UnlockMutex(wnd->mutex);
        
        // Throttle the execution until we are due for a new frame
        uint32_t t_current = SDL_GetTicks();
        if (t_current < t_next) {
            SDL_Delay(t_next - t_current);
        } else if (t_current > t_next) {
            //printf("%d %d\n", t_current, t_current - t_next);
            t_next = t_current;
        }
        t_next += FRAME_DURATION;
        frame++;
    }
    
    return frame;
}

// PUBLIC FUNCTIONS //

int window_init(Window *wnd, const char *filename, Machine *vm) {
    wnd->vm = vm;
    
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
    
    // Create window and renderer
    wnd->window = SDL_CreateWindow(filename, SDL_WINDOWPOS_UNDEFINED,
                                             SDL_WINDOWPOS_UNDEFINED,
                                             WIDTH_ADJUSTED, HEIGHT_CROPPED,
                                             SDL_WINDOW_ALLOW_HIGHDPI);
    if (!wnd->window) {
        printf("%s\n", SDL_GetError());
        return 1;
    }
    wnd->renderer = SDL_CreateRenderer(wnd->window, -1,
                                       SDL_RENDERER_ACCELERATED |
                                       SDL_RENDERER_PRESENTVSYNC);
    if (!wnd->renderer) {
        printf("%s\n", SDL_GetError());
        return 1;
    }

    // Compare physical resolution to display bounds to see if we can resize to
    // pixel-perfect (2048x1568) mode
    int w, h;
    SDL_GetRendererOutputSize(wnd->renderer, &w, &h);
    SDL_Rect bounds;
    SDL_GetDisplayUsableBounds(0, &bounds);
    int target_w = WIDTH_PP / (w / WIDTH_ADJUSTED);
    int target_h = HEIGHT_PP / (h / HEIGHT_CROPPED);
    if (target_w <= bounds.w && target_h <= bounds.h) {
        SDL_SetWindowSize(wnd->window, target_w, target_h);
    } else {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    }
    
    // Compute the display area
    SDL_GetRendererOutputSize(wnd->renderer, &w, &h);
    int zoom = h / HEIGHT_CROPPED + 1;
    int adjusted_w;
    do {
        adjusted_w = WIDTH * --zoom * 8 / 7;
        adjusted_w -= (adjusted_w % 2);
    } while (adjusted_w > w);
    wnd->display_area.w = adjusted_w;
    wnd->display_area.h = HEIGHT_CROPPED * zoom;
    wnd->display_area.x = (w - wnd->display_area.w) / 2;
    wnd->display_area.y = (h - wnd->display_area.h) / 2;
    
    wnd->texture = SDL_CreateTexture(wnd->renderer, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     WIDTH, HEIGHT);
    if (!wnd->texture) {
        printf("%s\n", SDL_GetError());
        return 1;
    }
    
    wnd->mutex = SDL_CreateMutex();
    if (!wnd->mutex) {
        printf("%s\n", SDL_GetError());
        return 1;
    }

    return 0;
}

void window_cleanup(Window *wnd) {
    SDL_DestroyMutex(wnd->mutex);
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

void window_loop(Window *wnd) {
    uint8_t *ctrls = wnd->vm->controllers;
    
    SDL_AtomicSet(&wnd->quitting, 0);
    SDL_Thread *t_machine = SDL_CreateThread(thread_machine_loop,
                                             "machine_loop", wnd);
    
    // Video and events loop
    uint32_t quit_request = 0;
    while (!SDL_AtomicGet(&wnd->quitting)) {
        // Process events
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
                        ctrls[cid] &= 0b1111;
                        wnd->js_use_axis[cid] = true;
                    }
                    if (event.jaxis.axis == 0) {
                        ctrls[cid] &= ~(BUTTON_LEFT | BUTTON_RIGHT);
                        if (event.jaxis.value < -AXIS_DEADZONE) {
                            ctrls[cid] |= BUTTON_LEFT;
                        } else if (event.jaxis.value > AXIS_DEADZONE) {
                            ctrls[cid] |= BUTTON_RIGHT;
                        }
                    } else if (event.jaxis.axis == 1) {
                        ctrls[cid] &= ~(BUTTON_UP | BUTTON_DOWN);
                        if (event.jaxis.value < -AXIS_DEADZONE) {
                            ctrls[cid] |= BUTTON_UP;
                        } else if (event.jaxis.value > AXIS_DEADZONE) {
                            ctrls[cid] |= BUTTON_DOWN;
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
                                ctrls[cid] &= 0b1111;
                                wnd->js_use_axis[cid] = false;
                            }
                            if (event.jbutton.state == SDL_PRESSED) {
                                ctrls[cid] |= 1 << i;
                            } else {
                                ctrls[cid] &= ~(1 << i);
                            }
                            break;
                        }
                    }
                    /*printf("P%d B%d:%d => %d\n", cid + 1,
                     event.jbutton.button, event.jbutton.state,
                     controllers[cid]);*/
                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.scancode) {
                        case SDL_SCANCODE_ESCAPE:
                            if (!quit_request) {
                                quit_request = SDL_GetTicks();
                            }
                            break;
                        default: break;
                    }
                    break;
                case SDL_KEYUP:
                    switch (event.key.keysym.scancode) {
                        case SDL_SCANCODE_ESCAPE:
                            quit_request = 0;
                            SDL_SetWindowOpacity(wnd->window, 1.0f);
                            break;
                        default: break;
                    }
                    break;
                case SDL_QUIT:
                    SDL_AtomicSet(&wnd->quitting, 1);
                    break;
            }
        }
        if (quit_request) {
            uint32_t elapsed = SDL_GetTicks() - quit_request;
            if (elapsed > QUIT_REQUEST_DELAY) {
                SDL_AtomicSet(&wnd->quitting, 1);
            } else {
                SDL_SetWindowOpacity(wnd->window,
                                     1.0f - (float)elapsed /
                                            (float)QUIT_REQUEST_DELAY);
            }
        }
        
        // Render the frame
        SDL_LockMutex(wnd->mutex);
        SDL_UpdateTexture(wnd->texture, NULL, wnd->screen_buffer,
                          WIDTH * sizeof(uint32_t));
        SDL_UnlockMutex(wnd->mutex);
        SDL_RenderClear(wnd->renderer);
        SDL_RenderCopy(wnd->renderer, wnd->texture, &screen_visible_area,
                       &wnd->display_area);
        SDL_RenderPresent(wnd->renderer);
    }
    
    int frame;
    SDL_WaitThread(t_machine, &frame);
    printf("Ended after %d frames\n", frame);
}
