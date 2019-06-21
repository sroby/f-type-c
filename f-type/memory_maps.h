#ifndef memory_maps_h
#define memory_maps_h

#include "common.h"

// Size of various memory structures
#define SIZE_WRAM 0x800
#define SIZE_PRG_ROM 0x8000
#define SIZE_CHR_ROM 0x2000
#define SIZE_NAMETABLE 0x400

// Forward declarations
typedef struct MemoryMap MemoryMap;
typedef struct PPUState PPUState;
typedef struct Cartridge Cartridge;

typedef struct {
    uint8_t (*read_func)(MemoryMap *, int);
    void (*write_func)(MemoryMap *, int, uint8_t);
    int offset;
} MemoryAddress;

struct MemoryMap {
    uint8_t last_read;
    void *internal;
    MemoryAddress addrs[0x10000];
};

typedef struct {
    PPUState *ppu;
    const uint8_t *prg_rom;
    uint8_t wram[SIZE_WRAM];
} MemoryMapCPUInternal;

typedef struct {
    const uint8_t *chr_rom;
    uint8_t nametables[2][SIZE_NAMETABLE];
    uint8_t *nt_layout[4];
    uint8_t background_colors[4];
    uint8_t palettes[8 * 3];
} MemoryMapPPUInternal;

void memory_map_cpu_init(MemoryMap *mm, MemoryMapCPUInternal *i,
                         const Cartridge *cart, PPUState *ppu);
void memory_map_ppu_init(MemoryMap *mm, MemoryMapPPUInternal *i,
                         const Cartridge *cart);

uint8_t mm_read(MemoryMap *mm, uint16_t addr);
uint16_t mm_read_word(MemoryMap *mm, uint16_t addr);

void mm_write(MemoryMap *mm, uint16_t addr, uint8_t value);
void mm_write_word(MemoryMap *mm, uint16_t addr, uint16_t value);

#endif /* memory_maps_h */
