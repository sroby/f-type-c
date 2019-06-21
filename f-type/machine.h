#ifndef machine_h
#define machine_h

#include "common.h"

// Timing constants
#define T_MULTI 3
#define T_SCANLINE_PER_CPU 341 // 113.666 * 3

typedef struct {
    uint16_t addr;
    char label[256];
} DebugMap;

typedef struct Cartridge {
    uint8_t *prg_rom;
    int prg_rom_size;
    uint8_t *chr_rom;
    int chr_rom_size;
    int mapper;
    bool mirroring;
} Cartridge;

void machine_loop(const Cartridge *cart, const DebugMap *dbg_map, bool verbose);

#endif /* system_h */
