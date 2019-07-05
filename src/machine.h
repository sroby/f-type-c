#ifndef machine_h
#define machine_h

#include "common.h"

#include "cpu.h"
#include "memory_maps.h"
#include "ppu.h"

// Timing constants
#define T_MULTI 3
#define T_SCANLINE_PER_CPU 341 // 113.666 * 3

// Forward decalarations
typedef struct Cartridge Cartridge;

typedef struct {
    uint16_t addr;
    char label[256];
} DebugMap;

typedef struct Machine {
    CPUState cpu;
    PPUState ppu;
    MemoryMap cpu_mm;
    MemoryMap ppu_mm;
    Cartridge *cart;
    const DebugMap *dbg_map;
} Machine;

void machine_init(Machine *vm, Cartridge *cart, const DebugMap *dbg_map);
void machine_advance_frame(Machine *vm, bool verbose);

#endif /* system_h */
