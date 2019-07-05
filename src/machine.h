#ifndef machine_h
#define machine_h

#include "common.h"

// Timing constants
#define T_MULTI 3
#define T_SCANLINE_PER_CPU 341 // 113.666 * 3

// Forward decalarations
typedef struct Window Window;
typedef struct Cartridge Cartridge;

typedef struct {
    uint16_t addr;
    char label[256];
} DebugMap;

void machine_loop(Cartridge *cart, const DebugMap *dbg_map, Window *wnd);

#endif /* system_h */
