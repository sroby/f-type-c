#ifndef memory_maps_h
#define memory_maps_h

#include "common.h"

// Size of various memory structures
#define SIZE_WRAM 0x800
#define SIZE_PRG_ROM 0x8000
#define SIZE_CHR_ROM 0x2000
#define SIZE_NAMETABLE 0x400

// Bit fields
#define BUTTON_A 1
#define BUTTON_B (1 << 1)
#define BUTTON_SELECT (1 << 2)
#define BUTTON_START (1 << 3)
#define BUTTON_UP (1 << 4)
#define BUTTON_DOWN (1 << 5)
#define BUTTON_LEFT (1 << 6)
#define BUTTON_RIGHT (1 << 7)

// Forward declarations
typedef struct MemoryMap MemoryMap;
typedef struct PPUState PPUState;
typedef struct Cartridge Cartridge;

typedef enum {
    SINGLE_A = 0,
    SINGLE_B = 1,
    HORIZONTAL = 2,
    VERTICAL = 3,
} NametableMirroring;

typedef struct {
    uint8_t (*read_func)(MemoryMap *, int);
    void (*write_func)(MemoryMap *, int, uint8_t);
    int offset;
} MemoryAddress;

typedef struct {
    PPUState *ppu;
    uint8_t wram[SIZE_WRAM];
    uint8_t controllers[2];
    int controller_bit;
} MemoryMapCPUData;

typedef struct {
    uint8_t nametables[2][SIZE_NAMETABLE];
    uint8_t *nt_layout[4];
    uint8_t background_colors[4];
    uint8_t palettes[8 * 3];
} MemoryMapPPUData;

typedef union {
    MemoryMapCPUData cpu;
    MemoryMapPPUData ppu;
} MemoryMapData;

struct MemoryMap {
    uint8_t last_read;
    MemoryMapData data;
    Cartridge *cart;
    MemoryAddress addrs[0x10000];
};

void memory_map_cpu_init(MemoryMap *mm, Cartridge *cart, PPUState *ppu);
void memory_map_ppu_init(MemoryMap *mm, Cartridge *cart);

uint8_t mm_read(MemoryMap *mm, uint16_t addr);
uint16_t mm_read_word(MemoryMap *mm, uint16_t addr);

void mm_write(MemoryMap *mm, uint16_t addr, uint8_t value);
void mm_write_word(MemoryMap *mm, uint16_t addr, uint16_t value);

void mm_ppu_set_nt_mirroring(MemoryMapPPUData *data, NametableMirroring m);

#endif /* memory_maps_h */
