#include "window.h"

#include "driver.h"

// Temporary mapping until it gets added to SDL
#define XMAP "0300000000f00000f100000000000000,RetroUSB.com SNES RetroPort,a:b3,b:b2,x:b1,y:b0,back:b4,start:b6,leftshoulder:b5,rightshoulder:b7,leftx:a0,lefty:a1"

// Button assignments
// A, B, Select, Start, Up, Down, Left, Right
static const SDL_GameControllerButton buttons[] = {
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
};
static const SDL_Scancode keys[] = {
    SDL_SCANCODE_X, SDL_SCANCODE_Z, SDL_SCANCODE_A, SDL_SCANCODE_S,
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
};

static bool get_env_bool(const char *name, bool *value) {
    const char *content = getenv(name);
    if (content) {
        *value = content[0] && strcmp(content, "0");
        return true;
    }
    return false;
}

static int identify_js(Window *wnd, SDL_JoystickID which) {
    SDL_GameController *js = SDL_GameControllerFromInstanceID(which);
    for (int i = 0; i < 2; i++) {
        if (wnd->js[i]) {
            if (wnd->js[i] == js) {
                return i;
            }
        }
    }
    return -1;
}

static void update_lightgun_pos(Driver *driver, const SDL_Rect *area,
                                int32_t x, int32_t y) {
    x -= area->x;
    y -= area->y;
    if (x >= 0 && y >= 0 && x < area->w && y < area->h) {
        driver->input.lightgun_pos = x * driver->screen_w / area->w +
                                     y * driver->screen_h / area->h * driver->screen_w;
    } else {
        driver->input.lightgun_pos = -1;
    }
}

static void audio_callback(Driver *driver, uint16_t *stream, int len) {
    len /= sizeof(int16_t);
    int pos = driver->audio_pos + 4096;
    for (int i = 0; i < len; i++) {
        pos %= 8192;
        stream[i] = driver->audio_buffer[pos++];
    }
}

static bool window_update_area(Window *wnd) {
    int w, h;
    SDL_GetRendererOutputSize(wnd->renderer, &w, &h);
    
    int zoom = h / wnd->driver->screen_h + 1;
    int adjusted_w;
    do {
        adjusted_w = wnd->driver->screen_w * --zoom * 8 / 7;
        adjusted_w -= (adjusted_w % 2);
    } while (adjusted_w > w);
    wnd->display_area.w = adjusted_w;
    wnd->display_area.h = wnd->driver->screen_h * zoom;
    wnd->display_area.x = (w - wnd->display_area.w) / 2;
    wnd->display_area.y = (h - wnd->display_area.h) / 2;
    int win_w, win_h;
    SDL_GetWindowSize(wnd->window, &win_w, &win_h);
    wnd->mouse_area.w = wnd->display_area.w * win_w / w;
    wnd->mouse_area.h = wnd->display_area.h * win_h / h;
    wnd->mouse_area.x = wnd->display_area.x * win_w / w;
    wnd->mouse_area.y = wnd->display_area.y * win_h / h;
    
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                (!wnd->fullscreen && (win_h == wnd->driver->screen_h)
                                      ? "best" : "nearest"));
    if (wnd->texture) {
        SDL_DestroyTexture(wnd->texture);
    }
    wnd->texture = SDL_CreateTexture(wnd->renderer, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     wnd->driver->screen_w,
                                     wnd->driver->screen_h);
    if (!wnd->texture) {
        eprintf("%s\n", SDL_GetError());
        return false;
    }
    return true;
}

int window_toggle_fullscreen(Window *wnd) {
    SDL_PauseAudioDevice(wnd->audio_id, 1);
    SDL_RenderClear(wnd->renderer);
    SDL_RenderPresent(wnd->renderer);
    
    wnd->fullscreen = !wnd->fullscreen;
    uint32_t flags = (wnd->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    int error_code = SDL_SetWindowFullscreen(wnd->window, flags);
    if (error_code) {
        eprintf("%s\n", SDL_GetError());
    } else {
        window_update_area(wnd);
    }
    
    SDL_PauseAudioDevice(wnd->audio_id, 0);
    return error_code;
}

// PUBLIC FUNCTIONS //

int window_init(Window *wnd, Driver *driver, const char *filename) {
    memset(wnd, 0, sizeof(Window));
    wnd->driver = driver;
    
    // Init SDL
    int error_code = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO |
                              SDL_INIT_GAMECONTROLLER);
    if (error_code) {
        eprintf("%s\n", SDL_GetError());
        return error_code;
    }
    
    // Attempt to open up to 2 controllers
    SDL_GameControllerAddMapping(XMAP);
    int n_js = SDL_NumJoysticks();
    if (n_js < 0) {
        eprintf("%s\n", SDL_GetError());
    }
    for (int i = 0; i < n_js; i++) {
        if (!SDL_IsGameController(i)) {
            continue;
        }
        SDL_GameController *js = SDL_GameControllerOpen(i);
        if (js) {
            const char *js_name = SDL_GameControllerName(js);
            char js_guid[33];
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i),
                                      js_guid, sizeof(js_guid));
            wnd->js[wnd->kb_assign++] = js;
            eprintf("Assigned \"%s\" (%s) as controller #%d\n",
                    js_name, js_guid, wnd->kb_assign);
            if (wnd->kb_assign >= 2) {
                wnd->kb_assign = -1;
                break;
            }
        } else {
            eprintf("%s\n", SDL_GetError());
        }
    }
    if (wnd->kb_assign >= 0) {
        eprintf("Assigned keyboard as controller #%d\n", wnd->kb_assign + 1);
    }
    
    // TODO: Everything below shouldn't assume a 8:7 anamorphic aspect ratio
    int width_adjusted = driver->screen_w * 8 / 7;
    
    // Create window and renderer
    wnd->window = SDL_CreateWindow(filename, SDL_WINDOWPOS_UNDEFINED,
                                             SDL_WINDOWPOS_UNDEFINED,
                                             width_adjusted, driver->screen_h,
                                             SDL_WINDOW_ALLOW_HIGHDPI);
    if (!wnd->window) {
        eprintf("%s\n", SDL_GetError());
        return 1;
    }
    wnd->renderer = SDL_CreateRenderer(wnd->window, -1,
                                       SDL_RENDERER_ACCELERATED |
                                       SDL_RENDERER_PRESENTVSYNC);
    if (!wnd->renderer) {
        eprintf("%s\n", SDL_GetError());
        return 1;
    }

    // Compare physical resolution to display bounds to see
    // if we can rezise to pixel-perfect (2048x1568) mode
    int w, h;
    SDL_GetRendererOutputSize(wnd->renderer, &w, &h);
    SDL_Rect bounds;
    SDL_GetDisplayUsableBounds(0, &bounds);
    int target_w = driver->screen_w * 8 / (w / width_adjusted);
    int target_h = driver->screen_h * 7 / (h / driver->screen_h);
    if (target_w <= bounds.w && target_h <= bounds.h) {
        SDL_SetWindowSize(wnd->window, target_w, target_h);
    }
    if (!window_update_area(wnd)) {
        return 1;
    }
    
    // Init sound
    SDL_AudioSpec desired, obtained;
    SDL_memset(&desired, 0, sizeof(desired));
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples = 4096;
    desired.callback = (SDL_AudioCallback) audio_callback;
    desired.userdata = driver;
    wnd->audio_id = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (wnd->audio_id <= 0) {
        eprintf("%s\n", SDL_GetError());
        return 1;
    }

    // Use the system crosshair cursor, if available
    wnd->cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    if (wnd->cursor) {
        SDL_SetCursor(wnd->cursor);
    } else {
        eprintf("%s\n", SDL_GetError());
    }
    
    return 0;
}

void window_cleanup(Window *wnd) {
    if (wnd->cursor) {
        SDL_FreeCursor(wnd->cursor);
    }
    SDL_CloseAudioDevice(wnd->audio_id);
    SDL_DestroyTexture(wnd->texture);
    SDL_DestroyRenderer(wnd->renderer);
    SDL_DestroyWindow(wnd->window);
    
    for (int i = 0; i < 2; i++) {
        if (wnd->js[i]) {
            SDL_GameControllerClose(wnd->js[i]);
        }
    }

    SDL_Quit();
}

void window_loop(Window *wnd) {
    bool verbose = false;
    get_env_bool("VERBOSE", &verbose);
    
    uint32_t *ctrls = wnd->driver->input.controllers;
    
    const uint64_t frame_length =
        (SDL_GetPerformanceFrequency() * 10000) / wnd->driver->refresh_rate;
    const uint64_t delay_div = SDL_GetPerformanceFrequency() / 1000;
    
    SDL_PauseAudioDevice(wnd->audio_id, 0);
    
    // Main loop
    int frame = 0;
    int quit_request = 0;
    uint64_t t_next = SDL_GetPerformanceCounter();
    while (true) {
        // Process events
        bool quitting = false;
        SDL_Event event;
        int cid;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_CONTROLLERAXISMOTION:
                    if (event.caxis.axis >= 2) {
                        break;
                    }
                    cid = identify_js(wnd, event.caxis.which);
                    if (cid < 0) {
                        break;
                    }
                    if (!wnd->js_use_axis[cid]) {
                        if (abs(event.caxis.value) < AXIS_DEADZONE) {
                            break;
                        }
                        ctrls[cid] &= 0b1111;
                        wnd->js_use_axis[cid] = true;
                    }
                    if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                        ctrls[cid] &= ~(BUTTON_LEFT | BUTTON_RIGHT);
                        if (event.caxis.value < -AXIS_DEADZONE) {
                            ctrls[cid] |= BUTTON_LEFT;
                        } else if (event.caxis.value > AXIS_DEADZONE) {
                            ctrls[cid] |= BUTTON_RIGHT;
                        }
                    } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                        ctrls[cid] &= ~(BUTTON_UP | BUTTON_DOWN);
                        if (event.caxis.value < -AXIS_DEADZONE) {
                            ctrls[cid] |= BUTTON_UP;
                        } else if (event.caxis.value > AXIS_DEADZONE) {
                            ctrls[cid] |= BUTTON_DOWN;
                        }
                    }
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                case SDL_CONTROLLERBUTTONUP:
                    cid = identify_js(wnd, event.cbutton.which);
                    if (cid < 0) {
                        break;
                    }
                    for (int i = 0; i < 8; i++) {
                        if (event.cbutton.button == buttons[i]) {
                            if (i > 3 && wnd->js_use_axis[cid]) {
                                ctrls[cid] &= 0b1111;
                                wnd->js_use_axis[cid] = false;
                            }
                            BIT_AS(ctrls[cid], i,
                                   event.cbutton.state == SDL_PRESSED);
                            break;
                        }
                    }
                    break;
                case SDL_MOUSEMOTION:
                    if (!(event.motion.state & SDL_BUTTON_RMASK)) {
                        update_lightgun_pos(wnd->driver, &wnd->mouse_area,
                                            event.motion.x, event.motion.y);
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button != SDL_BUTTON_LEFT &&
                        event.button.button != SDL_BUTTON_RIGHT) {
                        break;
                    }
                    wnd->driver->input.lightgun_trigger =
                        (event.button.state == SDL_PRESSED);
                    if (event.button.button == SDL_BUTTON_RIGHT) {
                        if (wnd->driver->input.lightgun_trigger) {
                            wnd->driver->input.lightgun_pos = -1;
                        } else {
                            update_lightgun_pos(wnd->driver, &wnd->mouse_area,
                                                event.button.x, event.button.y);
                        }
                    }
                    break;
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                    switch (event.key.keysym.scancode) {
                        case SDL_SCANCODE_ESCAPE:
                            if (event.key.state == SDL_PRESSED) {
                                if (wnd->fullscreen) {
                                    window_toggle_fullscreen(wnd);
                                } else if (!quit_request) {
                                    quit_request = frame;
                                }
                            } else {
                                quit_request = 0;
                                if (!wnd->fullscreen) {
                                    SDL_SetWindowOpacity(wnd->window, 1.0f);
                                }
                            }
                            break;
                        case SDL_SCANCODE_F:
                            if (event.key.state == SDL_PRESSED &&
                                !event.key.repeat) {
                                window_toggle_fullscreen(wnd);
                            }
                            break;
                        default:
                            if (wnd->kb_assign < 0) {
                                break;
                            }
                            for (int i = 0; i < 8; i++) {
                                if (event.key.keysym.scancode == keys[i]) {
                                    BIT_AS(ctrls[wnd->kb_assign], i,
                                           event.key.state == SDL_PRESSED);
                                }
                            }
                            break;
                    }
                    break;
                case SDL_QUIT:
                    quitting = true;
                    break;
            }
        }
        if (quitting) {
            break;
        }
        if (quit_request) {
            int elapsed = frame - quit_request;
            if (elapsed > QUIT_REQUEST_DELAY) {
                break;
            }
            SDL_SetWindowOpacity(wnd->window, 1.0f - (float)elapsed /
                                                     (float)QUIT_REQUEST_DELAY);
        }
        
        // Advance one frame
        (*wnd->driver->advance_frame_func)(wnd->driver->vm, verbose);
        
        // Render the frame unless we're behind schedule
        t_next += frame_length;
        int64_t t_left = t_next - SDL_GetPerformanceCounter();
        if (t_left > 0) {
            SDL_UpdateTexture(wnd->texture, NULL, wnd->driver->screen,
                              wnd->driver->screen_w * sizeof(uint32_t));
            SDL_RenderClear(wnd->renderer);
            SDL_RenderCopy(wnd->renderer, wnd->texture, NULL, &wnd->display_area);
            SDL_RenderPresent(wnd->renderer);
            
            // Add extra delay if we're more than one frame over schedule
            if (t_left > (frame_length + delay_div)) {
                SDL_Delay((uint32_t)((t_left - frame_length) / delay_div));
            }
        }/* else {
            printf("%lld\n", t_left);
        }*/
        
        frame++;
    }
    
    eprintf("Ended after %d frames\n", frame);
}
