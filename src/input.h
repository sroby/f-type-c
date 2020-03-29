#ifndef input_h
#define input_h

#include "common.h"

typedef struct InputState {
    uint32_t controllers[2];
    int lightgun_pos;
    bool lightgun_trigger;
} InputState;

#endif /* input_h */
