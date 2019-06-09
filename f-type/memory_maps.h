#ifndef memory_maps_h
#define memory_maps_h

#include <stdint.h>

#define SIZE_WRAM 0x800
#define SIZE_PRG_ROM 0x8000

typedef struct memory_map memory_map;

typedef struct {
    uint8_t (*read_func)(memory_map *, int);
    void (*write_func)(memory_map *, int, uint8_t);
    int offset;
} memory_address;

struct memory_map {
    const uint8_t *prg_rom;
    uint8_t wram[SIZE_WRAM];
    uint8_t last_read;
    memory_address addrs[0x10000];
};

void memory_map_cpu_init(memory_map *, const uint8_t *);

uint8_t mm_read(memory_map *, uint16_t);
uint16_t mm_read_word(memory_map *, uint16_t);

void mm_write(memory_map *, uint16_t, uint8_t);
void mm_write_word(memory_map *, uint16_t, uint16_t);

#endif /* memory_maps_h */
