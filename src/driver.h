#ifndef driver_h
#define driver_h

#include "common.h"
#include "input.h"

typedef struct Driver Driver;

typedef void (*AdvanceFrameFuncPtr)(void *, bool);
typedef uint32_t *(*GetScreenFuncPtr)(void *);

typedef struct Driver {
    void *vm;
    InputState input;
    uint64_t refresh_rate;
    uint32_t *screen;
    int screen_w;
    int screen_h;
    int16_t audio_buffer[8192];
    int audio_pos;
    AdvanceFrameFuncPtr advance_frame_func;
    void (*teardown_func)(Driver *);
} Driver;

#endif /* driver_h */
