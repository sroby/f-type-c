#ifndef machine_h
#define machine_h

#include "common.h"

// Size of various memory structures
#define SIZE_WRAM 0x800
#define SIZE_PRG_ROM 0x8000
#define SIZE_CHR_ROM 0x2000
#define SIZE_NAMETABLE 0x400

// Timing constants
#define T_CPU_MULTIPLIER 3

// IRQ lines
#define IRQ_MAPPER 1

// Forward decalarations
typedef struct Cartridge Cartridge;
typedef struct CPUState CPUState;
typedef struct MemoryMap MemoryMap;
typedef struct PPUState PPUState;

typedef struct {
    uint16_t addr;
    char label[256];
} DebugMap;

typedef struct Machine {
    CPUState *cpu;
    PPUState *ppu;
    MemoryMap *cpu_mm;
    MemoryMap *ppu_mm;
    Cartridge *cart;
    const DebugMap *dbg_map;
    
    // System RAM
    uint8_t wram[SIZE_WRAM];
    uint8_t nametables[4][SIZE_NAMETABLE];
    uint8_t *nt_layout[4];
    
    // Controller I/O
    uint8_t controllers[2];
    int controller_bit[2];
    bool lightgun_trigger;
    bool vs_bank;
} Machine;

typedef enum {
    NT_SINGLE_A = 0,
    NT_SINGLE_B = 1,
    NT_VERTICAL = 2,
    NT_HORIZONTAL = 3,
    NT_FOUR = 4,
} NametableMirroring;

void machine_init(Machine *vm, CPUState *cpu, PPUState *ppu, MemoryMap *cpu_mm,
                  MemoryMap *ppu_mm, Cartridge *cart, const DebugMap *dbg_map);

bool machine_advance_frame(Machine *vm, bool verbose);

void machine_set_nt_mirroring(Machine *vm, NametableMirroring m);

#endif /* system_h */
