#ifndef mappers_h
#define mappers_h

#include "common.h"

// Forward declarations
typedef struct MemoryMap MemoryMap;

typedef struct Cartridge {
    uint8_t *prg_rom;
    int prg_rom_size;
    uint8_t *chr_memory;
    int chr_memory_size;
    bool chr_is_ram;
    bool mirroring;
    int mapper_id;
    
    // Generic mapper banking
    int prg_bank;
    int prg_bank_size;
    int chr_bank;
    int chr_bank_size;
} Cartridge;

typedef struct MapperInfo {
    int ines_id;
    const char *name;
    void (*init_func)(MemoryMap *, MemoryMap *);
} MapperInfo;

bool mapper_check_support(int mapper_id, const char **name);

void mapper_init(Cartridge *cart, MemoryMap *cpu_mm, MemoryMap *ppu_mm);

#endif /* mappers_h */
