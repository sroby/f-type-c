#include "window.h"

#include "driver.h"

// Button assignments
// A, B, Select, Start, Up, Down, Left, Right
// Default is PS4/Switch, others are specific checks of the controller name
static const int buttons[] = {0, 2, 4, 6, 11, 12, 13, 14};
static const int buttons_snes_retroport[] = {2, 0, 4, 6, -1, -1, -1, -1};
static const int buttons_8bitdo[] = {0, 1, 10, 11, -1, -1, -1, -1};
static const int buttons_ds3[] = {14, 15, 0, 3, 4, 6, 7, 5};

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

// PUBLIC FUNCTIONS //

int window_init(Window *wnd, Driver *driver, const char *filename) {
    // Init SDL
    int error_code = SDL_Init(SDL_INIT_VIDEO |
                              SDL_INIT_AUDIO |
                              SDL_INIT_JOYSTICK);
    if (error_code) {
        eprintf("%s\n", SDL_GetError());
        return error_code;
    }
    
    // Attempt to open up to 2 controllers
    wnd->js_use_axis[0] = wnd->js_use_axis[1] = false;
    wnd->js[0] = wnd->js[1] = NULL;
    int n_js = SDL_NumJoysticks();
    if (n_js < 0) {
        eprintf("%s\n", SDL_GetError());
    }
    int assigned_js = 0;
    for (int i = 0; i < n_js; i++) {
        SDL_Joystick *js = SDL_JoystickOpen(i);
        if (js) {
            const char *js_name = SDL_JoystickName(js);
            char js_guid[33];
            SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js),
                                      js_guid, sizeof(js_guid));
            if (!strcasecmp(js_name, "retrousb.com snes retroport")) {
                wnd->buttons[assigned_js] = buttons_snes_retroport;
            } else if (!strncasecmp(js_name, "8bitdo", 6)) {
                wnd->buttons[assigned_js] = buttons_8bitdo;
            } else if (!strcasecmp(js_name, "playstation(r)3 controller")) {
                wnd->buttons[assigned_js] = buttons_ds3;
            } else {
                wnd->buttons[assigned_js] = buttons;
            }
            wnd->js[assigned_js++] = js;
            eprintf("Assigned \"%s\" (%s) as controller #%d\n",
                    js_name, js_guid, assigned_js);
            if (assigned_js >= 2) {
                break;
            }
        } else {
            eprintf("%s\n", SDL_GetError());
        }
    }
    if (!assigned_js) {
        eprintf("No controllers were found, will continue without input\n");
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

    // Compare physical resolution to display bounds to see if we can resize to
    // pixel-perfect (2048x1680) mode
    int w, h;
    SDL_GetRendererOutputSize(wnd->renderer, &w, &h);
    SDL_Rect bounds;
    SDL_GetDisplayUsableBounds(0, &bounds);
    int target_w = driver->screen_w * 8 / (w / width_adjusted);
    int target_h = driver->screen_h * 7 / (h / driver->screen_h);
    if (target_w <= bounds.w && target_h <= bounds.h) {
        SDL_SetWindowSize(wnd->window, target_w, target_h);
    } else {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    }
    
    // Compute the display area
    SDL_GetRendererOutputSize(wnd->renderer, &w, &h);
    int zoom = h / driver->screen_h + 1;
    int adjusted_w;
    do {
        adjusted_w = driver->screen_w * --zoom * 8 / 7;
        adjusted_w -= (adjusted_w % 2);
    } while (adjusted_w > w);
    wnd->display_area.w = adjusted_w;
    wnd->display_area.h = driver->screen_h * zoom;
    wnd->display_area.x = (w - wnd->display_area.w) / 2;
    wnd->display_area.y = (h - wnd->display_area.h) / 2;
    int win_w, win_h;
    SDL_GetWindowSize(wnd->window, &win_w, &win_h);
    wnd->mouse_area.w = wnd->display_area.w * win_w / w;
    wnd->mouse_area.h = wnd->display_area.h * win_h / h;
    wnd->mouse_area.x = wnd->display_area.x * win_w / w;
    wnd->mouse_area.y = wnd->display_area.y * win_h / h;

    wnd->texture = SDL_CreateTexture(wnd->renderer, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     driver->screen_w, driver->screen_h);
    if (!wnd->texture) {
        eprintf("%s\n", SDL_GetError());
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
            SDL_JoystickClose(wnd->js[i]);
        }
    }

    SDL_Quit();
}

void window_loop(Window *wnd, Driver *driver) {
    const char *const verb_char = getenv("VERBOSE");
    const bool verbose = verb_char ? *verb_char - '0' : false;
    uint32_t *ctrls = driver->input.controllers;
    
    const uint64_t frame_length =
        (SDL_GetPerformanceFrequency() * 10000) / driver->refresh_rate;
    const uint64_t delay_div = SDL_GetPerformanceFrequency() / 1000;
    
    SDL_PauseAudioDevice(wnd->audio_id, 0);
    
    // Main loop
    int frame = 0;
    int quit_request = 0;
    uint64_t t_next = SDL_GetPerformanceCounter();
    while(true) {
        // Process events
        bool quitting = false;
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
                    /*printf("P%d A%d:%d => %d\n", cid + 1, event.jaxis.axis,
                           event.jaxis.value, ctrls[cid]);*/
                    break;
                case SDL_JOYBUTTONDOWN:
                case SDL_JOYBUTTONUP:
                    cid = identify_js(wnd, event.jbutton.which);
                    if (cid < 0) {
                        break;
                    }
                    for (int i = 0; i < 8; i++) {
                        if (event.jbutton.button == wnd->buttons[cid][i]) {
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
                           event.jbutton.button,
                           event.jbutton.state, ctrls[cid]);*/
                    break;
                case SDL_MOUSEMOTION:
                    if (!(event.motion.state & SDL_BUTTON_RMASK)) {
                        update_lightgun_pos(driver, &wnd->mouse_area,
                                            event.motion.x, event.motion.y);
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button != SDL_BUTTON_LEFT &&
                        event.button.button != SDL_BUTTON_RIGHT) {
                        break;
                    }
                    driver->input.lightgun_trigger =
                        (event.button.state == SDL_PRESSED);
                    if (event.button.button == SDL_BUTTON_RIGHT) {
                        if (driver->input.lightgun_trigger) {
                            driver->input.lightgun_pos = -1;
                        } else {
                            update_lightgun_pos(driver, &wnd->mouse_area,
                                                event.button.x, event.button.y);
                        }
                    }
                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.scancode) {
                        case SDL_SCANCODE_ESCAPE:
                            if (!quit_request) {
                                quit_request = frame;
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
                    quitting = true;
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
        (*driver->advance_frame_func)(driver->vm, verbose);
        
        // Render the frame unless we're behind schedule
        t_next += frame_length;
        int64_t t_left = t_next - SDL_GetPerformanceCounter();
        if (t_left > 0) {
            SDL_UpdateTexture(wnd->texture, NULL, driver->screen,
                              driver->screen_w * sizeof(uint32_t));
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
