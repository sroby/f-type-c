#ifndef memory_maps_h
#define memory_maps_h

#include "common.h"

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
typedef struct Machine Machine;

typedef struct {
    uint8_t (*read_func)(Machine *, int);
    void (*write_func)(Machine *, int, uint8_t);
    int offset;
} MemoryAddress;

typedef struct MemoryMap {
    Machine *vm;
    uint8_t last_read;
    uint16_t addr_mask;
    MemoryAddress addrs[0x10000];
} MemoryMap;

void memory_map_cpu_init(MemoryMap *mm, Machine *vm);
void memory_map_ppu_init(MemoryMap *mm, Machine *vm);

uint8_t mm_read(MemoryMap *mm, uint16_t addr);
uint16_t mm_read_word(MemoryMap *mm, uint16_t addr);

void mm_write(MemoryMap *mm, uint16_t addr, uint8_t value);
void mm_write_word(MemoryMap *mm, uint16_t addr, uint16_t value);

#endif /* memory_maps_h */
