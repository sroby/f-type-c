#ifndef memory_maps_h
#define memory_maps_h

#include <stdint.h>

#define SIZE_WRAM 0x800
#define SIZE_PRG_ROM 0x8000

typedef struct MemoryMap MemoryMap;

typedef struct {
    uint8_t (*read_func)(MemoryMap *mm, int offset);
    void (*write_func)(MemoryMap *mm, int offset, uint8_t value);
    int offset;
} MemoryAddress;

struct MemoryMap {
    const uint8_t *prg_rom;
    uint8_t wram[SIZE_WRAM];
    uint8_t last_read;
    MemoryAddress addrs[0x10000];
};

void memory_map_cpu_init(MemoryMap *mm, const uint8_t *prg_rom);

uint8_t mm_read(MemoryMap *mm, uint16_t addr);
uint16_t mm_read_word(MemoryMap *mm, uint16_t addr);

void mm_write(MemoryMap *mm, uint16_t addr, uint8_t value);
void mm_write_word(MemoryMap *mm, uint16_t addr, uint16_t value);

#endif /* memory_maps_h */
