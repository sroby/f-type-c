#ifndef memory_maps_h
#define memory_maps_h

#include <stdint.h>

#define SIZE_WRAM 0x800
#define SIZE_PRG_ROM 0x8000

typedef struct MemoryMap MemoryMap;

typedef struct {
    uint8_t (*read_func)(MemoryMap *, int);
    void (*write_func)(MemoryMap *, int, uint8_t);
    int offset;
} MemoryAddress;

struct MemoryMap {
    const uint8_t *prg_rom;
    uint8_t wram[SIZE_WRAM];
    uint8_t last_read;
    MemoryAddress addrs[0x10000];
};

void memory_map_cpu_init(MemoryMap *, const uint8_t *);

uint8_t mm_read(MemoryMap *, uint16_t);
uint16_t mm_read_word(MemoryMap *, uint16_t);

void mm_write(MemoryMap *, uint16_t, uint8_t);
void mm_write_word(MemoryMap *, uint16_t, uint16_t);

#endif /* memory_maps_h */
