#ifndef f_memory_maps_h
#define f_memory_maps_h

#include "../common.h"

#define MASK_COLOR 0b111111

// Forward declarations
typedef struct Machine Machine;

typedef uint8_t (*ReadFuncPtr)(Machine *, uint16_t);
typedef void (*WriteFuncPtr)(Machine *, uint16_t, uint8_t);

typedef struct MemoryMap {
    Machine *vm;
    uint8_t last_read;
    uint16_t addr_mask;
    ReadFuncPtr read[0x10000];
    WriteFuncPtr write[0x10000];
} MemoryMap;

void memory_map_cpu_init(MemoryMap *mm, Machine *vm);
void memory_map_ppu_init(MemoryMap *mm, Machine *vm);

uint8_t mm_read(MemoryMap *mm, uint16_t addr);
uint16_t mm_read_word(MemoryMap *mm, uint16_t addr);

void mm_write(MemoryMap *mm, uint16_t addr, uint8_t value);
void mm_write_word(MemoryMap *mm, uint16_t addr, uint16_t value);

#endif /* f_memory_maps_h */
