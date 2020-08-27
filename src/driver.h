#ifndef driver_h
#define driver_h

#include "common.h"
#include "input.h"

#define MSG_NONE 0
#define MSG_TERMINATE 1

typedef struct Driver Driver;

typedef void (*AdvanceFrameFuncPtr)(void *, int, bool);
typedef void (*TeardownFuncPtr)(Driver *);

typedef struct Driver {
    void *vm;
    InputState input;
    uint64_t refresh_rate;
    uint32_t *screens[2];
    int screen_w;
    int screen_h;
    int frame;
    int16_t audio_buffer[8192];
    int audio_pos;
    AdvanceFrameFuncPtr advance_frame_func;
    TeardownFuncPtr teardown_func;
    int message;
} Driver;

#endif /* driver_h */
