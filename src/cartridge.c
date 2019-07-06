#include "cartridge.h"

#include "memory_maps.h"
#include "ppu.h"

// GENERIC MAPPER I/O //

static uint8_t read_fixed_prg(MemoryMap *mm, int offset) {
    return mm->cart->prg_rom[offset];
}
static uint8_t read_banked_prg(MemoryMap *mm, int offset) {
    offset += mm->cart->prg_bank * mm->cart->prg_bank_size;
    return mm->cart->prg_rom[offset];
}

static uint8_t read_fixed_chr(MemoryMap *mm, int offset) {
    return mm->cart->chr_memory[offset];
}
static uint8_t read_banked_chr(MemoryMap *mm, int offset) {
    offset += mm->cart->chr_bank * mm->cart->chr_bank_size;
    return mm->cart->chr_memory[offset];
}

static void write_fixed_chr(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->chr_memory[offset] = value;
}
static void write_banked_chr(MemoryMap *mm, int offset, uint8_t value) {
    offset += mm->cart->chr_bank * mm->cart->chr_bank_size;
    mm->cart->chr_memory[offset] = value;
}

static uint8_t read_sram(MemoryMap *mm, int offset) {
    if (mm->cart->sram_enabled) {
        offset += mm->cart->sram_bank * SIZE_SRAM;
        return mm->cart->sram[offset];
    }
    return mm->last_read;
}
void write_sram(MemoryMap *mm, int offset, uint8_t value) {
    if (mm->cart->sram_enabled) {
        offset += mm->cart->sram_bank * SIZE_SRAM;
        mm->cart->sram[offset] = value;
    }
}

static void init_fixed_prg(MemoryMap *mm, void (*register_func)
                                               (MemoryMap *, int, uint8_t)) {
    // 8000-FFFF: PRG ROM (32kB, repeated if 16kB)
    for (int i = 0; i < SIZE_PRG_ROM; i++) {
        mm->addrs[0x8000 + i] = (MemoryAddress)
            {read_fixed_prg, register_func, i % mm->cart->prg_rom_size};
    }
}
static void init_banked_prg(MemoryMap *mm, void (*register_func)
                                                (MemoryMap *, int, uint8_t)) {
    // 8000-FFFF: PRG ROM (32kB, repeated if 16kB)
    for (int i = 0; i < SIZE_PRG_ROM; i++) {
        mm->addrs[0x8000 + i] = (MemoryAddress)
            {read_banked_prg, register_func, i % mm->cart->prg_rom_size};
    }
}

static void init_fixed_chr(MemoryMap *mm) {
    // 0000-1FFF: CHR ROM (8kB)
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        mm->addrs[i] = (MemoryAddress) {read_fixed_chr,
            (mm->cart->chr_is_ram ? write_fixed_chr : NULL), i};
    }
}
static void init_banked_chr(MemoryMap *mm) {
    // 0000-1FFF: CHR ROM (8kB)
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        mm->addrs[i] = (MemoryAddress) {read_banked_chr,
            (mm->cart->chr_is_ram ? write_banked_chr : NULL), i};
    }
}

static void init_sram(MemoryMap *mm, int size) {
    Cartridge *cart = mm->cart;
    cart->sram_size = size;
    cart->sram = malloc(size);
    
    // 6000-7FFF: SRAM (up to 8kB, repeated if less)
    for (int i = 0; i < SIZE_SRAM; i++) {
        mm->addrs[0x6000 + i] = (MemoryAddress)
            {read_sram, write_sram, i % size};
    }
}

// MAPPER 0: NROM (aka. no mapper) //

static void NROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    init_fixed_prg(cpu_mm, NULL);
    init_fixed_chr(ppu_mm);
    
    // Used by Family BASIC only
    if (cpu_mm->cart->has_battery_backup) {
        init_sram(cpu_mm, SIZE_SRAM / 2);
    }
}

// MAPPER 1: Nintendo MMC1 (banked PRG/CHR, mirroring via serial interface) //

static uint8_t MMC1_read_prg(MemoryMap *mm, int offset) {
    Cartridge *cart = mm->cart;
    if (cart->prg_bank_size == SIZE_PRG_ROM) {
        offset += (cart->prg_bank & ~1) * cart->prg_bank_size;
    } else if (offset >= cart->prg_bank_size) {
        offset -= cart->prg_bank_size;
        if (cart->mapper.mmc1.prg_bank_mode) {
            offset += cart->prg_rom_size - cart->prg_bank_size;
        } else {
            offset += cart->prg_bank * cart->prg_bank_size;
        }
    } else if (cart->mapper.mmc1.prg_bank_mode) {
        offset += cart->prg_bank * cart->prg_bank_size;
    }
    return cart->prg_rom[offset];
}

static void MMC1_write_register(MemoryMap *mm, int offset, uint8_t value) {
    MMC1State *mmc1 = &mm->cart->mapper.mmc1;
    if (value & (1 << 7)) {
        mmc1->shift_reg = mmc1->shift_pos = 0;
        return;
    }
    mmc1->shift_reg |= (value & 1) << mmc1->shift_pos;
    mmc1->shift_pos++;
    if (mmc1->shift_pos < 5) {
        return;
    }
    switch (offset / 0x2000) {
        case 0: // 8000-9FFF: Control
            mm_ppu_set_nt_mirroring(&mm->data.cpu.ppu->mm->data.ppu,
                                    mmc1->shift_reg & 0b11);
            mmc1->prg_bank_mode = mmc1->shift_reg & (1 << 2);
            mm->cart->prg_bank_size =
                (mmc1->shift_reg & (1 << 3) ? SIZE_PRG_ROM / 2 : SIZE_PRG_ROM);
            mm->cart->chr_bank_size =
                (mmc1->shift_reg & (1 << 4) ? SIZE_CHR_ROM / 2 : SIZE_CHR_ROM);
            break;
        case 1: // A000-BFFF: CHR bank 0
            mm->cart->chr_bank = mmc1->shift_reg;
            break;
        case 2: // C000-DFFF: CHR bank 1
            mmc1->chr_bank_0x1000 = mmc1->shift_reg;
            break;
        case 3: // E000-FFFF: PRG bank + SRAM write protect
            mm->cart->prg_bank = mmc1->shift_reg & 0b1111;
            if (!mmc1->is_a) {
                mm->cart->sram_enabled = !(mmc1->shift_reg & (1 << 4));
            }
            break;
    }
    mmc1->shift_reg = mmc1->shift_pos = 0;
}

static void MMC1_chr_offset(Cartridge *cart, int *offset) {
    if (cart->chr_bank_size == SIZE_CHR_ROM) {
        *offset += (cart->chr_bank & ~1) * cart->chr_bank_size;
    } else if (*offset >= cart->chr_bank_size) {
        *offset += cart->mapper.mmc1.chr_bank_0x1000 * cart->chr_bank_size -
                   cart->chr_bank_size;
    } else {
        *offset += cart->chr_bank * cart->chr_bank_size;
    }
}
static uint8_t MMC1_read_chr(MemoryMap *mm, int offset) {
    MMC1_chr_offset(mm->cart, &offset);
    return mm->cart->chr_memory[offset];
}
static void MMC1_write_chr(MemoryMap *mm, int offset, uint8_t value) {
    MMC1_chr_offset(mm->cart, &offset);
    mm->cart->chr_memory[offset] = value;
}

static void MMC1_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    Cartridge *cart = cpu_mm->cart;
    memset(&cart->mapper.mmc1, 0, sizeof(MMC1State));
    
    // Booting in 16b+16f PRG mode seems to be the most compatible
    cart->prg_bank_size = SIZE_PRG_ROM / 2;
    cart->mapper.mmc1.prg_bank_mode = true;
    
    for (int i = 0; i < SIZE_PRG_ROM; i++) {
        cpu_mm->addrs[0x8000 + i] = (MemoryAddress)
            {MMC1_read_prg, MMC1_write_register, i};
    }
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        ppu_mm->addrs[i] = (MemoryAddress)
            {MMC1_read_chr, (cart->chr_is_ram ? MMC1_write_chr : NULL), i};
    }
    
    init_sram(cpu_mm, SIZE_SRAM);
}

// Variant 155: MMC1A
static void MMC1A_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    MMC1_init(cpu_mm, ppu_mm);
    cpu_mm->cart->mapper.mmc1.is_a = true;
}

// MAPPER 2: UxROM (bank switchable + fixed PRG ROM) //

static void UxROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    // TODO: Limit maximum page to size of PRG ROM
    mm->cart->prg_bank = value;
}

static void UxROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    cpu_mm->cart->prg_bank_size = SIZE_PRG_ROM / 2;
    
    const int last_bank = cpu_mm->cart->prg_rom_size - SIZE_PRG_ROM / 2;
    // CPU 8000-BFFF: Switchable bank
    // CPU C000-FFFF: Fixed to the last bank
    for (int i = 0; i < SIZE_PRG_ROM / 2; i++) {
        cpu_mm->addrs[0x8000 + i] = (MemoryAddress)
            {read_banked_prg, UxROM_write_register, i};
        cpu_mm->addrs[0x8000 + SIZE_PRG_ROM / 2 + i] = (MemoryAddress)
            {read_fixed_prg, UxROM_write_register, last_bank + i};
    }

    init_fixed_chr(ppu_mm);
}

// MAPPER 3: CNROM (bank switchable CHR ROM) //

static void CNROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->chr_bank = value & 0b11;
}

static void CNROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    init_fixed_prg(cpu_mm, CNROM_write_register);
    init_banked_chr(ppu_mm);
}

// MAPPER 7: AxROM (bank switchable PRG ROM, single page nametable toggle) //

static void AxROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->prg_bank = value & 0b111;
    mm_ppu_set_nt_mirroring(&mm->data.cpu.ppu->mm->data.ppu,
                            (value & 0b10000 ? NT_SINGLE_B : NT_SINGLE_A));
}

static void AxROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    mm_ppu_set_nt_mirroring(&cpu_mm->data.cpu.ppu->mm->data.ppu, NT_SINGLE_A);
    init_banked_prg(cpu_mm, AxROM_write_register);
    init_fixed_chr(ppu_mm);
}

// MAPPER 13: CPROM (fixed + bank switchable CHR RAM) //

static void CPROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->chr_bank = value & 0b11;
}

static void CPROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    Cartridge *cart = cpu_mm->cart;
    cart->chr_bank_size = SIZE_CHR_ROM / 2;
    
    // Expand CHR RAM to 16kB
    cart->chr_is_ram = true;
    cart->chr_memory_size = SIZE_CHR_ROM * 2;
    cart->chr_memory = realloc(cart->chr_memory, cart->chr_memory_size);
    
    init_fixed_prg(cpu_mm, CPROM_write_register);
    
    // PPU 0000-0FFF: Fixed to the first bank
    // PPU 1000-1FFF: Switchable bank
    for (int i = 0; i < SIZE_CHR_ROM / 2; i++) {
        ppu_mm->addrs[i] = (MemoryAddress)
            {read_fixed_chr, write_fixed_chr, i};
        ppu_mm->addrs[SIZE_CHR_ROM / 2 + i] = (MemoryAddress)
            {read_banked_chr, write_banked_chr, i};
    }
}

// MAPPER 34: BNROM (bank switchable PRG ROM) //

static void BNROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->prg_bank = value;
}

static void BNROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    init_banked_prg(cpu_mm, BNROM_write_register);
    init_fixed_chr(ppu_mm);
}

// MAPPER 66: GxROM (bank switchable PRG ROM and CHR ROM) //

static void GxROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->prg_bank = value >> 4;
    mm->cart->chr_bank = value & 0b1111;
}

static void GxROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    init_banked_prg(cpu_mm, GxROM_write_register);
    init_banked_chr(ppu_mm);
}

// MAPPER ENUMERATION ARRAY //

static const MapperInfo mappers[] = {
    // Nintendo discrete logic
    {  0, "NROM" ,  NROM_init},
    {  2, "UxROM", UxROM_init},
    {  3, "CNROM", CNROM_init},
    {  7, "AxROM", AxROM_init},
    { 13, "CPROM", CPROM_init},
    { 34, "BNROM", BNROM_init},
    { 66, "GxROM", GxROM_init},
    // Nintendo ASIC
    {  1, "MMC1" ,  MMC1_init},
    {155, "MMC1A", MMC1A_init},
};
static const size_t mappers_len = sizeof(mappers) / sizeof(MapperInfo);

// PUBLIC FUNCTIONS //

bool mapper_check_support(int mapper_id, const char **name) {
    for (int i = 0; i < mappers_len; i++) {
        if (mappers[i].ines_id == mapper_id) {
            *name = mappers[i].name;
            return true;
        }
    }
    return false;
}

void mapper_init(Cartridge *cart, MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    cart->prg_bank = cart->chr_bank = cart->sram_bank = 0;
    cart->prg_bank_size = SIZE_PRG_ROM;
    cart->chr_bank_size = SIZE_CHR_ROM;
    cart->sram_enabled = true;
    
    for (int i = 0; i < mappers_len; i++) {
        if (mappers[i].ines_id == cart->mapper_id) {
            (*mappers[i].init_func)(cpu_mm, ppu_mm);
            break;
        }
    }
}
