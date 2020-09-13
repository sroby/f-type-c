#ifndef f_cartridge_h
#define f_cartridge_h

#include "../common.h"

#define SIZE_SRAM 0x2000
#define MASK_SRAM (SIZE_SRAM - 1)

#define PRG_BANKS 4
#define SIZE_PRG_BANK (SIZE_PRG_ROM / PRG_BANKS)
#define MASK_PRG_BANK (SIZE_PRG_BANK - 1)

#define CHR_BANKS 8
#define SIZE_CHR_BANK (SIZE_CHR_ROM / CHR_BANKS)
#define MASK_CHR_BANK (SIZE_CHR_BANK - 1)

// Forward declarations
typedef struct Machine Machine;

typedef struct MMC1State {
    int shift_pos;
    uint8_t shift_reg;
    uint8_t ctrl_flags;
    uint8_t prg_bank;
    uint8_t chr_banks[2];
    bool is_a;
} MMC1State;

typedef struct MMC24State {
    int chr_banks[2][2];
    bool chr_latches[2];
    bool is_2;
} MMC24State;

typedef struct MMC3State {
    int bank_select;
    int banks[8];
    int irq_latch;
    int irq_counter;
    bool irq_enabled;
    bool last_pt;
} MMC3State;

typedef struct UxROMVariants {
    int bit_offset;
    int target_bank;
} UxROMVariants;

typedef struct Sunsoft4State {
    uint8_t ctrl;
    uint8_t *chr_nt_banks[2];
} Sunsoft4State;

typedef union Mapper {
    MMC1State mmc1;
    MMC24State mmc24;
    MMC3State mmc3;
    UxROMVariants uxrom;
    Sunsoft4State sunsoft4;
    int cp_counter;
    uint8_t vrc1_chr_banks[2];
    void (*hijacked_reg)(Machine *, uint16_t, uint8_t);
} Mapper;

typedef struct Cartridge {
    // PRG ROM
    blob prg_rom;
    uint8_t *prg_banks[4];
    
    // CHR ROM/RAM
    blob chr_memory;
    bool chr_is_ram;
    uint8_t *chr_banks[8];
    
    // SRAM (aka. PRG RAM)
    blob sram;
    bool sram_enabled;
    bool has_battery_backup;
    
    // Memory mapper
    Mapper mapper;
} Cartridge;

typedef struct MapperInfo {
    int ines_id;
    const char *name;
    void (*init_func)(Machine *);
} MapperInfo;

bool mapper_check_support(int mapper_id, const char **name);

void mapper_init(Machine *vm, int mapper_id);

#endif /* f_cartridge_h */
