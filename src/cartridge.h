#ifndef mappers_h
#define mappers_h

#include "common.h"

// Forward declarations
typedef struct MemoryMap MemoryMap;

typedef union MapperData {
    int uxrom_bank;
} MapperData;

typedef struct Cartridge {
    uint8_t *prg_rom;
    int prg_rom_size;
    uint8_t *chr_memory;
    int chr_memory_size;
    bool chr_is_ram;
    bool mirroring;
    int mapper_id;
    MapperData mapper;
} Cartridge;

typedef struct MapperInfo {
    int ines_id;
    const char *name;
    void (*init_func)(Cartridge *, MemoryMap *, MemoryMap *);
} MapperInfo;

bool mapper_check_support(int mapper_id, const char **name);

bool mapper_init(Cartridge *cart, MemoryMap *cpu_mm, MemoryMap *ppu_mm);

#endif /* mappers_h */
