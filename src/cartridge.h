#ifndef mappers_h
#define mappers_h

#include "common.h"

#define SIZE_SRAM 0x2000

#define MAX_BANKS 8

// Forward declarations
typedef struct MemoryMap MemoryMap;

typedef struct MMC1State {
    int shift_reg;
    int shift_pos;
    int prg_fixed_bank;
    int prg_bank;
    int chr_banks[2];
    bool is_a;
} MMC1State;

typedef struct MMC24State {
    int chr_banks[2][2];
    int chr_latches[2];
    bool is_2;
} MMC24State;

typedef union Mapper {
    MMC1State mmc1;
    MMC24State mmc24;
} Mapper;

typedef struct Cartridge {
    // PRG ROM
    uint8_t *prg_rom;
    int prg_rom_size;
    
    // CHR ROM/RAM
    uint8_t *chr_memory;
    int chr_memory_size;
    bool chr_is_ram;
    
    // SRAM (aka. PRG RAM)
    uint8_t *sram;
    int sram_size;
    bool sram_enabled;
    bool has_battery_backup;
    
    // Memory mapper
    bool mirroring;
    int mapper_id;
    Mapper mapper;
    int prg_banks[MAX_BANKS];
    int prg_bank_size;
    int chr_banks[MAX_BANKS];
    int chr_bank_size;
    int sram_bank;
} Cartridge;

typedef struct MapperInfo {
    int ines_id;
    const char *name;
    void (*init_func)(MemoryMap *, MemoryMap *);
} MapperInfo;

bool mapper_check_support(int mapper_id, const char **name);

void mapper_init(Cartridge *cart, MemoryMap *cpu_mm, MemoryMap *ppu_mm);

#endif /* mappers_h */
