#ifndef cartridge_h
#define cartridge_h

#include "common.h"

#define SIZE_SRAM 0x2000

#define MAX_BANKS 8

// Forward declarations
typedef struct Machine Machine;

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

typedef struct MMC3State {
    int bank_select;
    int banks[8];
    int irq_latch;
    int irq_counter;
    bool irq_enabled;
    bool irq_reload;
    bool last_pt;
} MMC3State;

typedef struct UxROMVariants {
    int bit_offset;
    int target_bank;
} UxROMVariants;

typedef union Mapper {
    MMC1State mmc1;
    MMC24State mmc24;
    MMC3State mmc3;
    UxROMVariants uxrom;
    uint8_t sunsoft4_ctrl;
    int cp_counter;
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
    int default_mirroring;
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
    void (*init_func)(Machine *);
} MapperInfo;

bool mapper_check_support(int mapper_id, const char **name);

void mapper_init(Machine *vm);

#endif /* cartridge_h */
