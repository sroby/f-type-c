#include "cartridge.h"

#include "cpu.h"
#include "memory_maps.h"
#include "ppu.h"

// GENERIC MAPPER I/O //

static uint8_t read_fixed_prg(MemoryMap *mm, int offset) {
    return mm->cart->prg_rom[offset];
}
static uint8_t read_banked_prg(MemoryMap *mm, int offset) {
    offset = offset % mm->cart->prg_bank_size +
             mm->cart->prg_banks[offset / mm->cart->prg_bank_size] *
             mm->cart->prg_bank_size;
    return mm->cart->prg_rom[offset];
}

static uint8_t read_fixed_chr(MemoryMap *mm, int offset) {
    return mm->cart->chr_memory[offset];
}
static uint8_t read_banked_chr(MemoryMap *mm, int offset) {
    offset = offset % mm->cart->chr_bank_size +
             mm->cart->chr_banks[offset / mm->cart->chr_bank_size] *
             mm->cart->chr_bank_size;
    return mm->cart->chr_memory[offset];
}

static void write_fixed_chr(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->chr_memory[offset] = value;
}
static void write_banked_chr(MemoryMap *mm, int offset, uint8_t value) {
    offset = offset % mm->cart->chr_bank_size +
             mm->cart->chr_banks[offset / mm->cart->chr_bank_size] *
             mm->cart->chr_bank_size;
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

static void MMC1_update_prg_banks(Cartridge *cart) {
    if (cart->prg_bank_size == SIZE_PRG_ROM) {
        cart->prg_banks[0] = cart->mapper.mmc1.prg_bank & ~1;
    } else if (cart->mapper.mmc1.prg_fixed_bank) {
        cart->prg_banks[0] = cart->mapper.mmc1.prg_bank;
        cart->prg_banks[1] = cart->prg_rom_size / cart->prg_bank_size - 1;
    } else {
        cart->prg_banks[0] = 0;
        cart->prg_banks[1] = cart->mapper.mmc1.prg_bank;
    }
}

static void MMC1_update_chr_banks(Cartridge *cart) {
    if (cart->chr_bank_size == SIZE_CHR_ROM) {
        cart->chr_banks[0] = cart->mapper.mmc1.chr_banks[0] & ~1;
    } else {
        cart->chr_banks[0] = cart->mapper.mmc1.chr_banks[0];
        cart->chr_banks[1] = cart->mapper.mmc1.chr_banks[1];
    }
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
    offset /= 0x2000;
    switch (offset) {
        case 0: // 8000-9FFF: Control
            mm_ppu_set_nt_mirroring(&mm->data.cpu.ppu->mm->data.ppu,
                                    mmc1->shift_reg & 0b11);
            mmc1->prg_fixed_bank = (mmc1->shift_reg & 0b100) << 2;
            mm->cart->prg_bank_size =
                (mmc1->shift_reg & (1 << 3) ? SIZE_PRG_ROM / 2 : SIZE_PRG_ROM);
            mm->cart->chr_bank_size =
                (mmc1->shift_reg & (1 << 4) ? SIZE_CHR_ROM / 2 : SIZE_CHR_ROM);
            MMC1_update_prg_banks(mm->cart);
            MMC1_update_chr_banks(mm->cart);
            break;
        case 1: // A000-BFFF: CHR bank 0
        case 2: // C000-DFFF: CHR bank 1
            mmc1->chr_banks[offset - 1] = mmc1->shift_reg;
            MMC1_update_chr_banks(mm->cart);
            break;
        case 3: // E000-FFFF: PRG bank + SRAM write protect
            mmc1->prg_bank = mmc1->shift_reg & 0b1111;
            if (!mmc1->is_a) {
                mm->cart->sram_enabled = !(mmc1->shift_reg & (1 << 4));
            }
            MMC1_update_prg_banks(mm->cart);
            break;
    }
    mmc1->shift_reg = mmc1->shift_pos = 0;
}

static void MMC1_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    Cartridge *cart = cpu_mm->cart;
    memset(&cart->mapper.mmc1, 0, sizeof(MMC1State));
    
    // Booting in 16b+16f PRG mode seems to be the most compatible
    cart->prg_bank_size = SIZE_PRG_ROM / 2;
    cart->mapper.mmc1.prg_fixed_bank = 1;
    MMC1_update_prg_banks(cart);
    
    init_banked_prg(cpu_mm, MMC1_write_register);
    init_banked_chr(ppu_mm);
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
    mm->cart->prg_banks[0] = value;
}

static void UxROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    Cartridge *cart = cpu_mm->cart;
    cart->prg_bank_size = SIZE_PRG_ROM / 2;
    
    // Page 1: Fixed to last bank
    cart->prg_banks[1] = cart->prg_rom_size / cart->prg_bank_size - 1;
    
    init_banked_prg(cpu_mm, UxROM_write_register);
    init_fixed_chr(ppu_mm);
}

// MAPPER 3: CNROM (bank switchable CHR ROM) //

static void CNROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->chr_banks[0] = value & 0b11;
}

static void CNROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    init_fixed_prg(cpu_mm, CNROM_write_register);
    init_banked_chr(ppu_mm);
}

// MAPPER 4: Nintendo MMC3 (and MMC6)                               //
//           (banked+fixed PRG, banked CHR, scanline counter, etc.) //

static void MMC3_scanline_callback(PPUState *ppu) {
    MMC3State *mmc = &ppu->mm->cart->mapper.mmc3;
    if (mmc->irq_counter == 0 && !mmc->irq_reload) {
        mmc->irq_reload = true;
        if (mmc->irq_enabled) {
            cpu_irq(ppu->cpu);
        }
    }
    if (mmc->irq_reload) {
        mmc->irq_counter = mmc->irq_latch;
        mmc->irq_reload = false;
    } else {
        mmc->irq_counter--;
    }
}

static void MMC3_update_banks(Cartridge *cart) {
    MMC3State *mmc = &cart->mapper.mmc3;
    
    // PRG ROM
    const int next_to_last = cart->prg_rom_size / cart->prg_bank_size - 2;
    if (mmc->bank_select & (1 << 6)) {
        cart->prg_banks[2] = mmc->banks[6];
        cart->prg_banks[1] = mmc->banks[7];
        cart->prg_banks[0] = next_to_last;
    } else {
        cart->prg_banks[0] = mmc->banks[6];
        cart->prg_banks[1] = mmc->banks[7];
        cart->prg_banks[2] = next_to_last;
    }
    
    // CHR ROM
    if (mmc->bank_select & (1 << 7)) {
        cart->chr_banks[4] = mmc->banks[0];
        cart->chr_banks[5] = mmc->banks[0] + 1;
        cart->chr_banks[6] = mmc->banks[1];
        cart->chr_banks[7] = mmc->banks[1] + 1;
        cart->chr_banks[0] = mmc->banks[2];
        cart->chr_banks[1] = mmc->banks[3];
        cart->chr_banks[2] = mmc->banks[4];
        cart->chr_banks[3] = mmc->banks[5];
    } else {
        cart->chr_banks[0] = mmc->banks[0];
        cart->chr_banks[1] = mmc->banks[0] + 1;
        cart->chr_banks[2] = mmc->banks[1];
        cart->chr_banks[3] = mmc->banks[1] + 1;
        cart->chr_banks[4] = mmc->banks[2];
        cart->chr_banks[5] = mmc->banks[3];
        cart->chr_banks[6] = mmc->banks[4];
        cart->chr_banks[7] = mmc->banks[5];
    }
}

static void MMC3_write_register(MemoryMap *mm, int offset, uint8_t value) {
    Cartridge *cart = mm->cart;
    MMC3State *mmc = &cart->mapper.mmc3;
    int bank;
    offset = offset / 0x2000 * 2 + offset % 2;
    switch (offset) {
        case 0: // Bank select
            cart->mapper.mmc3.bank_select = value;
            MMC3_update_banks(cart);
            break;
        case 1: // Bank data
            bank = mmc->bank_select & 0b111;
            if (bank < 2) {
                value &= ~1;
            }
            mmc->banks[bank] = value;
            MMC3_update_banks(cart);
            break;
        case 2: // Mirroring
            mm_ppu_set_nt_mirroring(&mm->data.cpu.ppu->mm->data.ppu,
                                    (value & 1 ? NT_HORIZONTAL : NT_VERTICAL));
            break;
        // 3: SRAM protect, intentionally not implemented
        case 4: // IRQ latch
            mmc->irq_latch = value;
            break;
        case 5: // IRQ reload
            mmc->irq_reload = true;
            break;
        case 6: // IRQ disable
            mmc->irq_enabled = false;
            break;
        case 7: // IRQ enable
            mmc->irq_enabled = true;
            break;
    }
}

static void MMC3_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    Cartridge *cart = cpu_mm->cart;
    cart->prg_bank_size = SIZE_PRG_ROM / 4;
    cart->chr_bank_size = SIZE_CHR_ROM / 8;
    memset(&cart->mapper.mmc3, 0, sizeof(MMC3State));
    
    // Last bank is fixed to the end
    cart->prg_banks[3] = cart->prg_rom_size / cart->prg_bank_size - 1;
    
    init_banked_prg(cpu_mm, MMC3_write_register);
    init_banked_chr(ppu_mm);
    init_sram(cpu_mm, SIZE_SRAM);
    
    cpu_mm->data.cpu.ppu->scanline_callback = MMC3_scanline_callback;
}

// MAPPER 7: AxROM (bank switchable PRG ROM, single page nametable toggle) //

static void AxROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->prg_banks[0] = value & 0b111;
    mm_ppu_set_nt_mirroring(&mm->data.cpu.ppu->mm->data.ppu,
                            (value & 0b10000 ? NT_SINGLE_B : NT_SINGLE_A));
}

static void AxROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    mm_ppu_set_nt_mirroring(&cpu_mm->data.cpu.ppu->mm->data.ppu, NT_SINGLE_A);
    init_banked_prg(cpu_mm, AxROM_write_register);
    init_fixed_chr(ppu_mm);
}

// MAPPERS 9-10: Nintendo MMC2 and MMC4                                      //
//               (banked+fixed PRG, banked CHR with read trigger, mirroring) //

static void MMC24_update_chr_banks(Cartridge *cart) {
    for (int i = 0; i < 2; i++) {
        cart->chr_banks[i] =
            cart->mapper.mmc24.chr_banks[i][cart->mapper.mmc24.chr_latches[i]];
    }
}

static void MMC24_write_register(MemoryMap *mm, int offset, uint8_t value) {
    offset = offset / 0x1000 - 2;
    if (offset < 0) {
        return;
    }
    switch (offset) {
        case 0: // Axxx: PRG ROM bank select
            mm->cart->prg_banks[0] = value & 0b1111;
            break;
        case 5: // Fxxx: Mirroring
            mm_ppu_set_nt_mirroring(&mm->data.cpu.ppu->mm->data.ppu,
                                    (value & 1 ? NT_HORIZONTAL : NT_VERTICAL));
            break;
        default: // Bxxx-Exxx: CHR ROM bank selects
            offset--;
            mm->cart->mapper.mmc24.chr_banks[offset / 2]
                                            [offset % 2] = value & 0b11111;
            MMC24_update_chr_banks(mm->cart);
    }
}

static uint8_t MMC24_read_chr(MemoryMap *mm, int offset) {
    uint8_t value = read_banked_chr(mm, offset);
    
    Cartridge *cart = mm->cart;
    MMC24State *mmc = &cart->mapper.mmc24;
    int bank = offset / cart->chr_bank_size;
    offset %= cart->chr_bank_size;
    if (mmc->is_2 && bank == 1) {
        if (offset >= 0x0FD8 && offset <= 0x0FDF) {
            mmc->chr_latches[bank] = 0;
        } else if (offset >= 0x0FE8 && offset <= 0x0FEF) {
            mmc->chr_latches[bank] = 1;
        }
    } else {
        if (offset == 0x0FD8) {
            mmc->chr_latches[bank] = 0;
        } else if (offset == 0x0FE8) {
            mmc->chr_latches[bank] = 1;
        }
    }
    MMC24_update_chr_banks(cart);
    
    return value;
}

static void MMC24_init_common(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    memset(&cpu_mm->cart->mapper.mmc24, 0, sizeof(MMC24State));
    
    init_banked_prg(cpu_mm, MMC24_write_register);
    
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        ppu_mm->addrs[i] = (MemoryAddress) {MMC24_read_chr, NULL, i};
    }
}

static void MMC2_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    MMC24_init_common(cpu_mm, ppu_mm);

    Cartridge *cart = cpu_mm->cart;
    cart->mapper.mmc24.is_2 = true;
    cart->prg_bank_size = SIZE_PRG_ROM / 4;
    cart->chr_bank_size = SIZE_CHR_ROM / 2;
    
    // Last three banks are fixed to the end
    const int n_banks = cart->prg_rom_size / cart->prg_bank_size;
    cart->prg_banks[1] = n_banks - 3;
    cart->prg_banks[2] = n_banks - 2;
    cart->prg_banks[3] = n_banks - 1;
}

static void MMC4_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    MMC24_init_common(cpu_mm, ppu_mm);
    
    Cartridge *cart = cpu_mm->cart;
    cart->prg_bank_size = SIZE_PRG_ROM / 2;
    cart->chr_bank_size = SIZE_CHR_ROM / 2;
    
    // Last bank is fixed to the end
    cart->prg_banks[1] = cart->prg_rom_size / cart->prg_bank_size - 1;
    
    init_sram(cpu_mm, SIZE_SRAM);
}

// MAPPER 13: CPROM (fixed + bank switchable CHR RAM) //

static void CPROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->chr_banks[1] = value & 0b11;
}

static void CPROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    Cartridge *cart = cpu_mm->cart;
    cart->chr_bank_size = SIZE_CHR_ROM / 2;
    
    // Expand CHR RAM to 16kB
    cart->chr_is_ram = true;
    cart->chr_memory_size = SIZE_CHR_ROM * 2;
    cart->chr_memory = realloc(cart->chr_memory, cart->chr_memory_size);
    
    init_fixed_prg(cpu_mm, CPROM_write_register);
    init_banked_chr(ppu_mm);
}

// MAPPER 34: BNROM (bank switchable PRG ROM) //

static void BNROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->prg_banks[0] = value;
}

static void BNROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    init_banked_prg(cpu_mm, BNROM_write_register);
    init_fixed_chr(ppu_mm);
}

// MAPPER 66: GxROM (bank switchable PRG ROM and CHR ROM) //

static void GxROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->prg_banks[0] = value >> 4;
    mm->cart->chr_banks[0] = value & 0b1111;
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
    {  4, "MMC3/MMC6", MMC3_init},
    {  9, "MMC2" ,  MMC2_init},
    { 10, "MMC4" ,  MMC4_init},
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
    memset(cart->prg_banks, 0, sizeof(cart->prg_banks));
    memset(cart->chr_banks, 0, sizeof(cart->chr_banks));
    cart->prg_bank_size = SIZE_PRG_ROM;
    cart->chr_bank_size = SIZE_CHR_ROM;
    cart->sram_enabled = true;
    cart->sram_bank = 0;

    for (int i = 0; i < mappers_len; i++) {
        if (mappers[i].ines_id == cart->mapper_id) {
            (*mappers[i].init_func)(cpu_mm, ppu_mm);
            break;
        }
    }
}
