#include "cartridge.h"

#include "cpu.h"
#include "machine.h"
#include "memory_maps.h"

// GENERIC MAPPER I/O //

static uint8_t read_fixed_prg(Machine *vm, int offset) {
    return vm->cart->prg_rom[offset];
}
static uint8_t read_banked_prg(Machine *vm, int offset) {
    offset = offset % vm->cart->prg_bank_size +
             vm->cart->prg_banks[offset / vm->cart->prg_bank_size] *
             vm->cart->prg_bank_size;
    return vm->cart->prg_rom[offset];
}

static uint8_t read_fixed_chr(Machine *vm, int offset) {
    return vm->cart->chr_memory[offset];
}
static uint8_t read_banked_chr(Machine *vm, int offset) {
    offset = offset % vm->cart->chr_bank_size +
             vm->cart->chr_banks[offset / vm->cart->chr_bank_size] *
             vm->cart->chr_bank_size;
    return vm->cart->chr_memory[offset];
}

static void write_fixed_chr(Machine *vm, int offset, uint8_t value) {
    vm->cart->chr_memory[offset] = value;
}
static void write_banked_chr(Machine *vm, int offset, uint8_t value) {
    offset = offset % vm->cart->chr_bank_size +
             vm->cart->chr_banks[offset / vm->cart->chr_bank_size] *
             vm->cart->chr_bank_size;
    vm->cart->chr_memory[offset] = value;
}

static uint8_t read_sram(Machine *vm, int offset) {
    if (vm->cart->sram_enabled) {
        offset += vm->cart->sram_bank * SIZE_SRAM;
        return vm->cart->sram[offset];
    }
    return vm->cpu_mm->last_read;
}
static void write_sram(Machine *vm, int offset, uint8_t value) {
    if (vm->cart->sram_enabled) {
        offset += vm->cart->sram_bank * SIZE_SRAM;
        vm->cart->sram[offset] = value;
    }
}

static void init_fixed_prg(Machine *vm, void (*register_func)
                                             (Machine *, int, uint8_t)) {
    // 8000-FFFF: PRG ROM (32kB, repeated if 16kB)
    for (int i = 0; i < SIZE_PRG_ROM; i++) {
        vm->cpu_mm->addrs[0x8000 + i] = (MemoryAddress)
            {read_fixed_prg, register_func, i % vm->cart->prg_rom_size};
    }
}
static void init_banked_prg(Machine *vm, void (*register_func)
                                              (Machine *, int, uint8_t)) {
    // 8000-FFFF: PRG ROM (32kB, repeated if 16kB)
    for (int i = 0; i < SIZE_PRG_ROM; i++) {
        vm->cpu_mm->addrs[0x8000 + i] = (MemoryAddress)
            {read_banked_prg, register_func, i % vm->cart->prg_rom_size};
    }
}

static void init_fixed_chr(Machine *vm) {
    // 0000-1FFF: CHR ROM (8kB)
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        vm->ppu_mm->addrs[i] = (MemoryAddress) {read_fixed_chr,
            (vm->cart->chr_is_ram ? write_fixed_chr : NULL), i};
    }
}
static void init_banked_chr(Machine *vm) {
    // 0000-1FFF: CHR ROM (8kB)
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        vm->ppu_mm->addrs[i] = (MemoryAddress) {read_banked_chr,
            (vm->cart->chr_is_ram ? write_banked_chr : NULL), i};
    }
}

static void init_sram(Machine *vm, int size) {
    vm->cart->sram_size = size;
    vm->cart->sram = malloc(size);
    
    // 6000-7FFF: SRAM (up to 8kB, repeated if less)
    for (int i = 0; i < SIZE_SRAM; i++) {
        vm->cpu_mm->addrs[0x6000 + i] = (MemoryAddress)
            {read_sram, write_sram, i % size};
    }
}

static void init_register_sram(Machine *vm,
         void (*register_func)(Machine *, int, uint8_t)) {
    for (int i = 0; i < SIZE_SRAM; i++) {
        vm->cpu_mm->addrs[0x6000 + i].write_func = register_func;
    }
}

static int extract_bank(int n_banks, int bit_offset, uint8_t value) {
    int i = 2;
    while (i < n_banks) {
        i <<= 1;
    }
    return (value >> bit_offset) & (i - 1);
}

static int extract_prg_bank(Cartridge *cart, int bit_offset, uint8_t value) {
    return extract_bank(cart->prg_rom_size / cart->prg_bank_size,
                        bit_offset, value);
}

static int extract_chr_bank(Cartridge *cart, int bit_offset, uint8_t value) {
    return extract_bank(cart->chr_memory_size / cart->chr_bank_size,
                        bit_offset, value);
}

static int get_last_prg_bank(Cartridge *cart) {
    return cart->prg_rom_size / cart->prg_bank_size - 1;
}

// MAPPER 0: Nintendo NROM (32f/8f, aka. no mapper) //

static void NROM_init(Machine *vm) {
    init_fixed_prg(vm, NULL);
    init_fixed_chr(vm);
    
    // Used by Family BASIC only
    if (vm->cart->has_battery_backup) {
        init_sram(vm, SIZE_SRAM / 2);
    }
}

// MAPPER   1: Nintendo MMC1 (variable banking, A/B/H/V control) //
//        155: Nintendo MMC1A (no SRAM protect toggle)           //

static void MMC1_update_prg_banks(Cartridge *cart) {
    if (cart->prg_bank_size == SIZE_PRG_ROM) {
        cart->prg_banks[0] = cart->mapper.mmc1.prg_bank >> 1;
    } else if (cart->mapper.mmc1.prg_fixed_bank) {
        cart->prg_banks[0] = cart->mapper.mmc1.prg_bank;
        cart->prg_banks[1] = get_last_prg_bank(cart);
    } else {
        cart->prg_banks[0] = 0;
        cart->prg_banks[1] = cart->mapper.mmc1.prg_bank;
    }
}

static void MMC1_update_chr_banks(Cartridge *cart) {
    if (cart->chr_bank_size == SIZE_CHR_ROM) {
        cart->chr_banks[0] = cart->mapper.mmc1.chr_banks[0] >> 1;
    } else {
        cart->chr_banks[0] = cart->mapper.mmc1.chr_banks[0];
        cart->chr_banks[1] = cart->mapper.mmc1.chr_banks[1];
    }
}

static void MMC1_write_register(Machine *vm, int offset, uint8_t value) {
    MMC1State *mmc1 = &vm->cart->mapper.mmc1;
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
            machine_set_nt_mirroring(vm, mmc1->shift_reg & 0b11);
            mmc1->prg_fixed_bank = (mmc1->shift_reg & 0b100) << 2;
            vm->cart->prg_bank_size =
                (mmc1->shift_reg & (1 << 3) ? SIZE_PRG_ROM / 2 : SIZE_PRG_ROM);
            vm->cart->chr_bank_size =
                (mmc1->shift_reg & (1 << 4) ? SIZE_CHR_ROM / 2 : SIZE_CHR_ROM);
            MMC1_update_prg_banks(vm->cart);
            MMC1_update_chr_banks(vm->cart);
            break;
        case 1: // A000-BFFF: CHR bank 0
        case 2: // C000-DFFF: CHR bank 1
            mmc1->chr_banks[offset - 1] = mmc1->shift_reg;
            MMC1_update_chr_banks(vm->cart);
            break;
        case 3: // E000-FFFF: PRG bank + SRAM write protect
            mmc1->prg_bank = mmc1->shift_reg & 0b1111;
            if (!mmc1->is_a) {
                vm->cart->sram_enabled = !(mmc1->shift_reg & (1 << 4));
            }
            MMC1_update_prg_banks(vm->cart);
            break;
    }
    mmc1->shift_reg = mmc1->shift_pos = 0;
}

static void MMC1_init(Machine *vm) {
    Cartridge *cart = vm->cart;
    memset(&cart->mapper.mmc1, 0, sizeof(MMC1State));
    
    // Booting in 16b+16f PRG mode seems to be the most compatible
    cart->prg_bank_size = SIZE_PRG_ROM / 2;
    cart->mapper.mmc1.prg_fixed_bank = 1;
    MMC1_update_prg_banks(cart);
    
    init_banked_prg(vm, MMC1_write_register);
    init_banked_chr(vm);
    init_sram(vm, SIZE_SRAM);
}

static void MMC1A_init(Machine *vm) {
    MMC1_init(vm);
    vm->cart->mapper.mmc1.is_a = true;
}

// MAPPER   2: Nintendo UxROM (16b+16f/8f)                              //
//         93: Sunsoft-2 IC on Sunsoft-3R board (register shift 4 bits) //
//         94: Nintendo UN1ROM (register shift 2 bits)                  //
//        180: Nintendo UNROM with 74HC08 (16f+16b/8f)                  //

static void UxROM_write_register(Machine *vm, int offset, uint8_t value) {
    vm->cart->prg_banks[vm->cart->mapper.uxrom.target_bank] =
        extract_prg_bank(vm->cart, vm->cart->mapper.uxrom.bit_offset, value);
}

static void UxROM_init(Machine *vm) {
    Cartridge *cart = vm->cart;
    cart->prg_bank_size = SIZE_PRG_ROM / 2;
    cart->prg_banks[1] = get_last_prg_bank(cart);
    memset(&cart->mapper.uxrom, 0, sizeof(UxROMVariants));
    
    init_banked_prg(vm, UxROM_write_register);
    init_fixed_chr(vm);
}

static void Sunsoft2R_init(Machine *vm) {
    UxROM_init(vm);
    vm->cart->mapper.uxrom.bit_offset = 4;
}

static void UN1ROM_init(Machine *vm) {
    UxROM_init(vm);
    vm->cart->mapper.uxrom.bit_offset = 2;
}

static void UNROM08_init(Machine *vm) {
    Cartridge *cart = vm->cart;
    cart->prg_bank_size = SIZE_PRG_ROM / 2;
    cart->mapper.uxrom.bit_offset = 0;
    cart->mapper.uxrom.target_bank = 1;
    
    init_banked_prg(vm, UxROM_write_register);
    init_fixed_chr(vm);
}

// MAPPER 3: Nintendo CNROM (32f/8b) //

static void CNROM_write_register(Machine *vm, int offset, uint8_t value) {
    vm->cart->chr_banks[0] = extract_chr_bank(vm->cart, 0, value);
}

static void CNROM_init(Machine *vm) {
    init_fixed_prg(vm, CNROM_write_register);
    init_banked_chr(vm);
}

// MAPPER   4: Nintendo MMC3 and MMC6                                       //
//             (variable banking, H/V control, scanline counter)            //
//        119: TQROM variant (uses both CHR ROM and CHR RAM simultaneously) //

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

static void MMC3_write_register(Machine *vm, int offset, uint8_t value) {
    Cartridge *cart = vm->cart;
    MMC3State *mmc = &cart->mapper.mmc3;
    int bank;
    offset = offset / 0x2000 * 2 + offset % 2;
    switch (offset) {
        case 0: // Bank select
            mmc->bank_select = value;
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
            machine_set_nt_mirroring(vm,
                                     (value & 1 ? NT_HORIZONTAL : NT_VERTICAL));
            break;
        // 3: SRAM protect, intentionally not implemented to ensure
        //    cross-compatibility with MMC6 which shares the same mapper ID
        case 4: // IRQ latch
            mmc->irq_latch = value;
            break;
        case 5: // IRQ reload
            mmc->irq_counter = 0;
            break;
        case 6: // IRQ disable
            mmc->irq_enabled = false;
            vm->cpu->irq &= ~IRQ_MAPPER;
            break;
        case 7: // IRQ enable
            mmc->irq_enabled = true;
            break;
    }
}

static uint8_t MMC3_read_chr(Machine *vm, int offset) {
    MMC3State *mmc = &vm->cart->mapper.mmc3;
    bool current_pt = offset & (1 << 12);
    if (!mmc->last_pt && current_pt) {
        if (mmc->irq_counter) {
            mmc->irq_counter--;
        } else {
            mmc->irq_counter = mmc->irq_latch;
        }
        if (!mmc->irq_counter && mmc->irq_enabled) {
            vm->cpu->irq |= IRQ_MAPPER;
        }
    }
    mmc->last_pt = current_pt;
    
    return read_banked_chr(vm, offset);
}

static void MMC3_init(Machine *vm) {
    Cartridge *cart = vm->cart;
    cart->prg_bank_size = SIZE_PRG_ROM / 4;
    cart->chr_bank_size = SIZE_CHR_ROM / 8;
    memset(&cart->mapper.mmc3, 0, sizeof(MMC3State));
    cart->prg_banks[3] = get_last_prg_bank(cart);
    MMC3_update_banks(cart);
    
    init_banked_prg(vm, MMC3_write_register);
    
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        vm->ppu_mm->addrs[i] = (MemoryAddress)
            {MMC3_read_chr, (cart->chr_is_ram ? write_banked_chr : NULL), i};
    }
    
    init_sram(vm, SIZE_SRAM);
}

static void MMC3Q_init(Machine *vm) {
    Cartridge *cart = vm->cart;
    
    // Change the CHR to RAM and grow it to 128kB, to match how the bank data
    // register maps the CHR RAM. It's a lazy hack but seems to work, both games
    // using this board still have issues but they may be caused by remaining
    // problems in the scanline counter?
    cart->chr_is_ram = true;
    cart->chr_memory_size = 16 * SIZE_CHR_ROM;
    cart->chr_memory = realloc(cart->chr_memory, cart->chr_memory_size);
    
    MMC3_init(vm);
}

// MAPPER 7: Nintendo AxROM (32b/8f, A/B control) //

static void AxROM_write_register(Machine *vm, int offset, uint8_t value) {
    vm->cart->prg_banks[0] = value & 0b111;
    machine_set_nt_mirroring(vm, (value & 0b10000 ? NT_SINGLE_B : NT_SINGLE_A));
}

static void AxROM_init(Machine *vm) {
    machine_set_nt_mirroring(vm, NT_SINGLE_A);
    init_banked_prg(vm, AxROM_write_register);
    init_fixed_chr(vm);
}

// MAPPER  9: Nintendo MMC2                                        //
//            (8b+24f/4b+4b, CHR bank read trigger, H/V control)   //
//        10: Nintendo MMC4                                        //
//            (similar but 16b+16f/4b+4b and simpler read trigger) //

static void MMC24_update_chr_banks(Cartridge *cart) {
    for (int i = 0; i < 2; i++) {
        cart->chr_banks[i] =
            cart->mapper.mmc24.chr_banks[i][cart->mapper.mmc24.chr_latches[i]];
    }
}

static void MMC24_write_register(Machine *vm, int offset, uint8_t value) {
    offset = offset / 0x1000 - 3;
    switch (offset) {
        case -1: // Axxx: PRG ROM bank select
            vm->cart->prg_banks[0] = extract_prg_bank(vm->cart, 0, value);
            break;
        case 4: // Fxxx: Mirroring
            machine_set_nt_mirroring(vm,
                                     (value & 1 ? NT_HORIZONTAL : NT_VERTICAL));
            break;
        default: // Bxxx-Exxx: CHR ROM bank selects
            vm->cart->mapper.mmc24.chr_banks[offset / 2][offset % 2] =
                extract_chr_bank(vm->cart, 0, value);
            MMC24_update_chr_banks(vm->cart);
    }
}

static uint8_t MMC24_read_chr(Machine *vm, int offset) {
    uint8_t value = read_banked_chr(vm, offset);
    
    Cartridge *cart = vm->cart;
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

static void MMC24_init_common(Machine *vm) {
    memset(&vm->cart->mapper.mmc24, 0, sizeof(MMC24State));
    
    init_banked_prg(vm, MMC24_write_register);
    
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        vm->ppu_mm->addrs[i] = (MemoryAddress) {MMC24_read_chr, NULL, i};
    }
}

static void MMC2_init(Machine *vm) {
    MMC24_init_common(vm);

    Cartridge *cart = vm->cart;
    cart->mapper.mmc24.is_2 = true;
    cart->prg_bank_size = SIZE_PRG_ROM / 4;
    cart->chr_bank_size = SIZE_CHR_ROM / 2;
    
    // Last three banks are fixed to the end
    const int n_banks = cart->prg_rom_size / cart->prg_bank_size;
    cart->prg_banks[1] = n_banks - 3;
    cart->prg_banks[2] = n_banks - 2;
    cart->prg_banks[3] = n_banks - 1;
}

static void MMC4_init(Machine *vm) {
    MMC24_init_common(vm);
    
    Cartridge *cart = vm->cart;
    cart->prg_bank_size = SIZE_PRG_ROM / 2;
    cart->chr_bank_size = SIZE_CHR_ROM / 2;
    cart->prg_banks[1] = get_last_prg_bank(cart);
    
    init_sram(vm, SIZE_SRAM);
}

// MAPPER 11: Color Dreams (32b/8b, similar to GxROM but reversed register) //

static void Color_Dreams_write_register(Machine *vm, int offset,
                                        uint8_t value) {
    vm->cart->prg_banks[0] = extract_prg_bank(vm->cart, 0, value);
    vm->cart->chr_banks[0] = extract_chr_bank(vm->cart, 4, value);
}

static void Color_Dreams_init(Machine *vm) {
    init_banked_prg(vm, Color_Dreams_write_register);
    init_banked_chr(vm);
}

// MAPPER 13: Nintendo CPROM (32f/4f+4b, 16kB CHR RAM) //

static void CPROM_write_register(Machine *vm, int offset, uint8_t value) {
    vm->cart->chr_banks[1] = value & 0b11;
}

static void CPROM_init(Machine *vm) {
    Cartridge *cart = vm->cart;
    cart->chr_bank_size = SIZE_CHR_ROM / 2;
    
    // Force CHR RAM and expand it to 16kB
    cart->chr_is_ram = true;
    cart->chr_memory_size = SIZE_CHR_ROM * 2;
    cart->chr_memory = realloc(cart->chr_memory, cart->chr_memory_size);
    
    init_fixed_prg(vm, CPROM_write_register);
    init_banked_chr(vm);
}

// MAPPER 34: Nintendo BNROM (32b/8f)  //
//        39: Unnamed Subor equivalent //

static void BNROM_write_register(Machine *vm, int offset, uint8_t value) {
    vm->cart->prg_banks[0] = extract_prg_bank(vm->cart, 0, value);
}

static void BNROM_init(Machine *vm) {
    init_banked_prg(vm, BNROM_write_register);
    init_fixed_chr(vm);
}

// MAPPER 38: PCI556 (32b/8b) //

static void PCI556_write_register(Machine *vm, int offset, uint8_t value) {
    vm->cart->prg_banks[0] = extract_prg_bank(vm->cart, 0, value);
    vm->cart->chr_banks[0] = extract_chr_bank(vm->cart, 2, value);
}

static void PCI556_init(Machine *vm) {
    init_banked_prg(vm, NULL);
    init_banked_chr(vm);
    
    // Register is only in the upper half of the SRAM area
    for (int i = 0x7000; i < 0x8000; i++) {
        vm->cpu_mm->addrs[i] = (MemoryAddress) {NULL, PCI556_write_register, 0};
    }
}

// MAPPER  66: Nintendo GNROM and MHROM (32b/8b)                          //
//         70: Bandai 74*161/161/32 (16b+16f/8b with equivalent register) //
//        140: Jaleco JF-11/14 (similar but register in the SRAM area)    //
//        152: Bandai 74*161/161/32 single screen (70 with A/B control)   //

static void GxROM_write_register(Machine *vm, int offset, uint8_t value) {
    vm->cart->prg_banks[0] = extract_prg_bank(vm->cart, 4, value);
    vm->cart->chr_banks[0] = extract_chr_bank(vm->cart, 0, value);
}

static void GxROM_init(Machine *vm) {
    init_banked_prg(vm, GxROM_write_register);
    init_banked_chr(vm);
}

static void Bandai74_init_common(Machine *vm) {
    Cartridge *cart = vm->cart;
    cart->prg_bank_size = SIZE_PRG_ROM / 2;
    cart->prg_banks[1] = get_last_prg_bank(cart);
}

static void Bandai74_init(Machine *vm) {
    Bandai74_init_common(vm);
    GxROM_init(vm);
}

static void JF1114_init(Machine *vm) {
    init_banked_prg(vm, NULL);
    init_banked_chr(vm);
    init_register_sram(vm, GxROM_write_register);
}

static void Bandai74s_write_register(Machine *vm, int offset, uint8_t value) {
    GxROM_write_register(vm, offset, value);
    machine_set_nt_mirroring(vm, (value & 128 ? NT_SINGLE_B : NT_SINGLE_A));
}

static void Bandai74s_init(Machine *vm) {
    Bandai74_init_common(vm);
    init_banked_prg(vm, Bandai74s_write_register);
    init_banked_chr(vm);
    machine_set_nt_mirroring(vm, NT_SINGLE_A);
}

// MAPPER 68: Sunsoft-4 (16b+16f/2b+2b+2b+2b, NT mapped to CHR) //

static void Sunsoft4_update_nametables(Machine *vm) {
    const int layouts[] = {
        0, 1, 0, 1, // VERTICAL
        0, 0, 1, 1, // HORIZONTAL
        0, 0, 0, 0, // SINGLE_A
        1, 1, 1, 1, // SINGLE_B
    };
    Cartridge *cart = vm->cart;
    const int *layout = layouts + (cart->mapper.sunsoft4_ctrl & 0b11) * 4;
    const bool chr_mode = cart->mapper.sunsoft4_ctrl & (1 << 4);
    for (int i = 0; i < 4; i++) {
        if (chr_mode) {
            vm->nt_layout[i] = cart->chr_memory + cart->chr_bank_size / 2 *
                               cart->chr_banks[layout[i] + 4];
        } else {
            vm->nt_layout[i] = vm->nametables[layout[i]];
        }
    }
}

static void Sunsoft4_write_register_chr(Machine *vm, int offset,
                                        uint8_t value) {
    offset = offset / 0x1000;
    if (offset >= 4) {
        value |= 128;
    }
    vm->cart->chr_banks[offset] = value;
    if (offset >= 4) {
        Sunsoft4_update_nametables(vm);
    }
}

static void Sunsoft4_write_register_ctrl(Machine *vm, int offset,
                                         uint8_t value) {
    vm->cart->mapper.sunsoft4_ctrl = value;
    Sunsoft4_update_nametables(vm);
}

static void Sunsoft4_write_register_prg(Machine *vm, int offset,
                                        uint8_t value) {
    vm->cart->prg_banks[0] = extract_prg_bank(vm->cart, 0, value);
    // TODO: bit 4 enable SRAM
}

static void Sunsoft4_write_nametables(Machine *vm, int offset, uint8_t value) {
    if (!(vm->cart->mapper.sunsoft4_ctrl & (1 << 4))) {
        vm->nt_layout[offset / SIZE_NAMETABLE]
                     [offset % SIZE_NAMETABLE] = value;
    }
}

static void Sunsoft4_init(Machine *vm) {
    Cartridge *cart = vm->cart;
    cart->prg_bank_size = SIZE_PRG_ROM / 2;
    cart->chr_bank_size = SIZE_CHR_ROM / 4;
    cart->mapper.sunsoft4_ctrl = 0;
    
    init_banked_prg(vm, NULL);
    for (int i = 0x8000; i < 0xE000; i++) {
        vm->cpu_mm->addrs[i].write_func = Sunsoft4_write_register_chr;
    }
    for (int i = 0xE000; i < 0xF000; i++) {
        vm->cpu_mm->addrs[i].write_func = Sunsoft4_write_register_ctrl;
    }
    for (int i = 0xF000; i < 0x10000; i++) {
        vm->cpu_mm->addrs[i].write_func = Sunsoft4_write_register_prg;
    }
    
    init_banked_chr(vm);
    init_sram(vm, SIZE_SRAM);
    
    vm->cart->prg_banks[1] = get_last_prg_bank(cart);
    
    // Need to enforce write protection when CHR ROM is mapped to NT
    for (int i = 0; i < 0x1EFF; i++) {
        vm->ppu_mm->addrs[0x2000 + i].write_func = Sunsoft4_write_nametables;
    }
}

// MAPPER  75: Konami VRC1 (8b+8b+8b+8f/4b+4b, H/V control) //
//        151: Duplicate (intended for Vs. System)          //

static void VRC1_write_register_prg(Machine *vm, int offset, uint8_t value) {
    vm->cart->prg_banks[offset / 0x2000] = extract_prg_bank(vm->cart, 0, value);
}

static void VRC1_write_register_misc(Machine *vm, int offset, uint8_t value) {
    machine_set_nt_mirroring(vm, (value & 1 ? NT_HORIZONTAL : NT_VERTICAL));
    vm->cart->chr_banks[0] = ((value & 0b10) << 3) |
                             (vm->cart->chr_banks[0] & 0b1111);
    vm->cart->chr_banks[1] = ((value & 0b100) << 2) |
                             (vm->cart->chr_banks[1] & 0b1111);
}

static void VRC1_write_register_chr(Machine *vm, int offset, uint8_t value) {
    int *bank = vm->cart->chr_banks + (offset / 0x1000 - 6);
    *bank = (*bank & 16) | (value & 0b1111);
}

static void VRC1_init(Machine *vm) {
    Cartridge *cart = vm->cart;
    cart->prg_bank_size = SIZE_PRG_ROM / 4;
    cart->chr_bank_size = SIZE_CHR_ROM / 2;
    cart->prg_banks[3] = get_last_prg_bank(cart);
    
    init_banked_prg(vm, NULL);
    for (int i = 0x8000; i < 0xE000; i += 0x2000) {
        for (int j = 0; j < 0x1000; j++) {
            vm->cpu_mm->addrs[i + j].write_func = VRC1_write_register_prg;
        }
    }
    for (int i = 0x9000; i < 0xA000; i++) {
        vm->cpu_mm->addrs[i].write_func = VRC1_write_register_misc;
    }
    for (int i = 0xE000; i < 0x10000; i++) {
        vm->cpu_mm->addrs[i].write_func = VRC1_write_register_chr;
    }
    
    init_banked_chr(vm);
}

// MAPPER  79: American Video Entertainment NINA-03/06 (32b/8b)      //
//        113: Multicart variant (larger bank capacity, H/V control) //
//        146: Duplicate of 79                                       //

static void NINA0306_write_register(Machine *vm, int offset, uint8_t value) {
    vm->cart->prg_banks[0] = extract_prg_bank(vm->cart, 3, value);
    vm->cart->chr_banks[0] = extract_chr_bank(vm->cart, 0, value);
}

static void NINA0306MC_write_register(Machine *vm, int offset, uint8_t value) {
    vm->cart->prg_banks[0] = extract_prg_bank(vm->cart, 3, value);
    vm->cart->chr_banks[0] = (value & 0b111) | ((value & 64) >> 3);
    machine_set_nt_mirroring(vm, (value & 128 ? NT_VERTICAL : NT_HORIZONTAL));
}

static void NINA0306_init_common(Machine *vm,
           void (*register_func)(Machine *, int, uint8_t)) {
    init_banked_prg(vm, NULL);
    init_banked_chr(vm);
    
    // The register is at a more complicated location but who cares
    for (int i = 0x4100; i < 0x6000; i++) {
        vm->cpu_mm->addrs[i].write_func = register_func;
    }
}

static void NINA0306_init(Machine *vm) {
    NINA0306_init_common(vm, NINA0306_write_register);
}

static void NINA0306MC_init(Machine *vm) {
    NINA0306_init_common(vm, NINA0306MC_write_register);
}

// MAPPER 87: Konami/Jaleco/Taito 74*139/74       //
//            (32f/8b, reversed bits in register) //

static void KJT74_write_register(Machine *vm, int offset, uint8_t value) {
    vm->cart->chr_banks[0] = extract_prg_bank(vm->cart, 0,
                                              ((value & 1) << 1) |
                                              ((value & 2) >> 1));
}

static void KJT74_init(Machine *vm) {
    init_fixed_prg(vm, NULL);
    init_banked_chr(vm);
    init_register_sram(vm, KJT74_write_register);
}

// MAPPER 89: Sunsoft-2 IC on Sunsoft-3 board (16b+16f/8b, A/B control) //

static void Sunsoft2_write_register(Machine *vm, int offset, uint8_t value) {
    vm->cart->prg_banks[0] = extract_prg_bank(vm->cart, 4, value);
    vm->cart->chr_banks[0] = ((value & 128) >> 4) | (value & 0b111);
    machine_set_nt_mirroring(vm, (value & 8 ? NT_SINGLE_B : NT_SINGLE_A));
}

static void Sunsoft2_init(Machine *vm) {
    Cartridge *cart = vm->cart;
    cart->prg_bank_size = SIZE_PRG_ROM / 2;
    cart->prg_banks[1] = get_last_prg_bank(cart);
    
    init_banked_prg(vm, Sunsoft2_write_register);
    init_banked_chr(vm);
    
    machine_set_nt_mirroring(vm, NT_SINGLE_A);
}

// MAPPER 97: Irem TAM-S1 (16f+16b/8f, A/B/H/V control) //

static void TAMS1_write_register(Machine *vm, int offset, uint8_t value) {
    const NametableMirroring modes[] = {NT_SINGLE_A, NT_HORIZONTAL,
                                        NT_VERTICAL, NT_SINGLE_B};
    vm->cart->prg_banks[1] = extract_prg_bank(vm->cart, 0, value);
    machine_set_nt_mirroring(vm, modes[value >> 6]);
}

static void TAMS1_init(Machine *vm) {
    Cartridge *cart = vm->cart;
    cart->prg_bank_size = SIZE_PRG_ROM / 2;
    // This is not a typo, it really fixes the *first* bank to the end
    cart->prg_banks[0] = get_last_prg_bank(cart);
    
    init_banked_prg(vm, TAMS1_write_register);
    init_fixed_chr(vm);
    
    machine_set_nt_mirroring(vm, NT_SINGLE_A);
}

// MAPPER 99: Nintendo Vs. System default board (8b+24f/8b via $4016 bit 2) //

static uint8_t VS_read_prg(Machine *vm, int offset) {
    if (vm->vs_bank) {
        offset += SIZE_PRG_ROM;
    }
    return vm->cart->prg_rom[offset];
}

static uint8_t VS_read_chr(Machine *vm, int offset) {
    if (vm->vs_bank) {
        offset += SIZE_CHR_ROM;
    }
    return vm->cart->chr_memory[offset];
}

static void VS_init(Machine *vm) {
    Cartridge *cart = vm->cart;
    
    for (int i = 0; i < SIZE_PRG_ROM / 4; i++) {
        vm->cpu_mm->addrs[0x8000 + i] = (MemoryAddress)
            {(cart->prg_rom_size > SIZE_PRG_ROM ? VS_read_prg : read_fixed_prg),
             NULL, i};
    }
    for (int i = SIZE_PRG_ROM / 4; i < SIZE_PRG_ROM; i++) {
        vm->cpu_mm->addrs[0x8000 + i] = (MemoryAddress)
            {read_fixed_prg, NULL, i};
    }
    
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        vm->ppu_mm->addrs[i] = (MemoryAddress)
        {(cart->chr_memory_size > SIZE_CHR_ROM ? VS_read_chr : NULL), NULL, i};
    }
    
    init_sram(vm, SIZE_SRAM);
}

// MAPPER 184: Sunsoft-1 (32f/4b+4b) //

static void Sunsoft1_write_register(Machine *vm, int offset, uint8_t value) {
    vm->cart->chr_banks[0] = extract_chr_bank(vm->cart, 0, value);
    vm->cart->chr_banks[1] = extract_chr_bank(vm->cart, 4, value);
}

static void Sunsoft1_init(Machine *vm) {
    vm->cart->chr_bank_size = SIZE_CHR_ROM / 2;
    
    init_fixed_prg(vm, NULL);
    init_banked_chr(vm);
    init_register_sram(vm, Sunsoft1_write_register);
}

// MAPPER 185: Nintendo CNROM, but abused as a crude copy protection scheme //
//             (all games are really just 32f/8f)                           //

static uint8_t CNROM_CP_read_chr(Machine *vm, int offset) {
    const uint8_t dummies[] = {0xBE, 0xEF};
    if (vm->cart->mapper.cp_counter < 2) {
        return dummies[vm->cart->mapper.cp_counter++];
    }
    return read_fixed_chr(vm, offset);
}

static void CNROM_CP_init(Machine *vm) {
    vm->cart->mapper.cp_counter = 0;
    
    init_fixed_prg(vm, NULL);
    
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        vm->ppu_mm->addrs[i] = (MemoryAddress) {CNROM_CP_read_chr,
            (vm->cart->chr_is_ram ? write_fixed_chr : NULL), i};
    }
}

// MAPPER ENUMERATION ARRAY //

static const MapperInfo mappers[] = {
    {  0, "Nintendo NROM", NROM_init},
    {  1, "Nintendo SxROM (MMC1)", MMC1_init},
    {  2, "Nintendo UxROM", UxROM_init},
    {  3, "Nintendo CNROM", CNROM_init},
    {  4, "Nintendo TxROM/HKROM (MMC3/MMC6)", MMC3_init},
    {  5, "Nintendo ExROM (MMC5)", NULL},
    {  7, "Nintendo AxROM", AxROM_init},
    {  9, "Nintendo PxROM (MMC2)", MMC2_init},
    { 10, "Nintendo FxROM (MMC4)", MMC4_init},
    { 11, "Color Dreams", Color_Dreams_init},
    { 12, "Supertone MMC3 clone", NULL},
    { 13, "Nintendo CPROM", CPROM_init},
    { 14, "Supertone MMC3 clone", NULL},
    { 16, "Bandai FCG series", NULL},
    { 18, "Jaleco SS88006", NULL},
    { 19, "Namco 129/163", NULL},
    { 21, "Konami VRC4a/c", NULL},
    { 22, "Konami VRC2a", NULL},
    { 23, "Konami VRC2b/4e", NULL},
    { 24, "Konami VRC6a", NULL},
    { 25, "Konami VRC2c/4b/4d", NULL},
    { 26, "Konami VRC6b", NULL},
    { 28, "Action 53", NULL},
    { 29, "Sealie Computing RET-CUFROM revD", NULL},
    { 30, "RetroUSB UNROM 512", NULL},
    { 31, "NSF compilation", NULL},
    { 32, "Irem G-101", NULL},
    { 33, "Taito TC0190", NULL},
    { 34, "Nintendo BNROM", BNROM_init},
    { 35, "J.Y. Company ASIC", NULL},
    { 36, "TXC Corporation 01-22000-400", NULL},
    { 37, "Nintendo MMC3 multicart", NULL},
    { 38, "Bit Corp. PCI556", PCI556_init},
    { 39, "Subor BNROM equivalent", BNROM_init},
    { 41, "NTDEC multicart", NULL},
    { 46, "Rumble Station", NULL},
    { 47, "Nintendo MMC3 multicart", NULL},
    { 48, "Taito TC0690", NULL},
    { 56, "Kaiser", NULL},
    { 61, "Sabor multicart", NULL},
    { 63, "NTDEC multicart", NULL},
    { 64, "Tengen RAMBO-1", NULL},
    { 65, "Irem H3001", NULL},
    { 66, "Nintendo GNROM/MHROM", GxROM_init},
    { 67, "Sunsoft-3", NULL},
    { 68, "Sunsoft-4", Sunsoft4_init},
    { 69, "Sunsoft FME-7", NULL},
    { 70, "Bandai 74*161/161/32", Bandai74_init},
    { 71, "Codemasters/Camerica UNROM clone", NULL},
    { 72, "Jaleco JF-17", NULL},
    { 73, "Konami VRC3", NULL},
    { 74, "Waixing MMC3 clone", NULL},
    { 75, "Konami VRC1", VRC1_init},
    { 76, "Namco NAMCOT-3446", NULL},
    { 77, "Irem 74*161/161/21/138", NULL},
    { 78, "Jaleco JF-16", NULL},
    { 79, "American Video Entertainment NINA-03/06", NINA0306_init},
    { 80, "Taito X1-005", NULL},
    { 81, "NTDEC", NULL},
    { 82, "Taito X1-017", NULL},
    { 83, "Cony", NULL},
    { 85, "Konami VRC-7", NULL},
    { 86, "Jaleco JF-13", NULL},
    { 87, "Konami/Jaleco/Taito 74*139/74", KJT74_init},
    { 88, "Namco 118 variant", NULL},
    { 89, "Sunsoft-2 on Sunsoft-3", Sunsoft2_init},
    { 90, "J.Y. Company ASIC", NULL},
    { 91, "J.Y. Company", NULL},
    { 92, "Jaleco JF-17 variant", NULL},
    { 93, "Sunsoft-2 on Sunsoft-3R", Sunsoft2R_init},
    { 94, "Nintendo UN1ROM", UN1ROM_init},
    { 95, "Namco NAMCOT-3425", NULL},
    { 96, "Bandai 74*161/02/74", NULL},
    { 97, "Irem TAM-S1", TAMS1_init},
    { 99, "Nintendo Vs. System", VS_init},
    {105, "Nintendo NES-EVENT (MMC1)", NULL},
    {107, "Magicseries", NULL},
    {111, "Membler Industries Cheapocabra (GTROM)", NULL},
    {112, "NTDEC DxROM clone", NULL},
    {113, "American Video Entertainment NINA-03/06 multicart", NINA0306MC_init},
    {115, "Kasheng SFC-02B/-03/-004", NULL},
    {116, "Supertone SOMARI-P Huang-1/2", NULL},
    {118, "Nintendo TxSROM (MMC3)", NULL},
    {119, "Nintendo TQROM (MMC3)", MMC3Q_init},
    {121, "Kasheng A9711/13", NULL},
    {123, "Kasheng H2288", NULL},
    {132, "TXC Corporation 01-22*", NULL},
    {133, "Sachen 3009 or 72008", NULL},
    {136, "Sachen 3011", NULL},
    {137, "Sachen 8249D", NULL},
    {138, "Sachen 8249D", NULL},
    {139, "Sachen 8249C", NULL},
    {140, "Jaleco JF-11/14", JF1114_init},
    {141, "Sachen 8249A", NULL},
    {142, "Kaiser", NULL},
    {143, "Sachen NROM clone with copy protection", NULL},
    {144, "Color Dreams variant", NULL},
    {145, "Sachen SA-72007", NULL},
    {146, "Sachen NINA-03/06 equivalent", NINA0306_init},
    {147, "Sachen 3018", NULL},
    {148, "Sachen NINA-06 variant", NULL},
    {149, "Sachen SA-0036", NULL},
    {150, "Sachen 74LS374N", NULL},
    {151, "Konami VRC1 on Vs. System", VRC1_init},
    {152, "Bandai 74*161/161/32 single screen", Bandai74s_init},
    {153, "Bandai FCG with LZ93D50", NULL},
    {154, "Namco NAMCOT-3453", NULL},
    {155, "Nintendo SxROM (MMC1A)", MMC1A_init},
    {156, "DAOU", NULL},
    {157, "Bandai FCG with LZ93D50", NULL},
    {158, "Tengen 800037", NULL},
    {159, "Bandai FCG with LZ93D50", NULL},
    {160, "Sachen", NULL},
    {163, "Nanjing", NULL},
    {164, "Waixing", NULL},
    {166, "Subor", NULL},
    {167, "Subor", NULL},
    {168, "Racermate", NULL},
    {171, "Kaiser KS-7058", NULL},
    {172, "TXC Corporation Super Mega P-4070", NULL},
    {173, "Idea-Tek ET.xx", NULL},
    {174, "NTDEC multicart", NULL},
    {175, "Kaiser", NULL},
    {176, "Waixing multicart", NULL},
    {177, "Hengedianzi", NULL},
    {178, "Waixing", NULL},
    {180, "Nintendo UNROM with 74HC08", UNROM08_init},
    {184, "Sunsoft-1", Sunsoft1_init},
    {185, "Nintendo CNROM Copy Protection", CNROM_CP_init},
    {186, "Fukutake Shoten", NULL},
    {188, "Bandai Karaoke Studio", NULL},
    {189, "Subor MMC3 clone", NULL},
    {190, "Zemina", NULL},
    {191, "Waixing MMC3 clone", NULL},
    {192, "Waixing MMC3 clone", NULL},
    {193, "NTDEC TC-112", NULL},
    {194, "Waixing MMC3 clone", NULL},
    {195, "Waixing FS303", NULL},
    {198, "Waixing", NULL},
    {199, "Waixing", NULL},
    {206, "Nintendo DxROM / Namco 118 / Tengen MIMIC-1", NULL},
    {207, "Taito X1-005 variant", NULL},
    {208, "Supertone", NULL},
    {209, "J.Y. Company ASIC", NULL},
    {210, "Namco 175/340", NULL},
    {211, "J.Y. Company ASIC", NULL},
    {218, "Magic Floor", NULL},
    {219, "Ka Sheng MMC3 clone", NULL},
    {221, "NTDEC N625092", NULL},
    {223, "Waixing", NULL},
    {224, "Jncota KT-008", NULL},
    {228, "Active Enterprises", NULL},
    {232, "Codemasters/Camerica Quattro", NULL},
    {234, "American Video Entertainment MAXI15", NULL},
    {240, "Computer & Entertainment", NULL},
    {241, "Subor", NULL},
    {242, "Waixing", NULL},
    {243, "Sachen 74LS374N", NULL},
    {244, "Computer & Entertainment", NULL},
    {245, "Waixing MMC3 clone", NULL},
    {246, "Computer & Entertainment", NULL},
    {248, "Kasheng SFC-02B/-03/-004", NULL},
    {249, "Waixing", NULL},
    {252, "Waixing VRC4 clone", NULL},
    {253, "Waixing VRC4 clone", NULL},
};
static const size_t mappers_len = sizeof(mappers) / sizeof(MapperInfo);

// PUBLIC FUNCTIONS //

bool mapper_check_support(int mapper_id, const char **name) {
    for (int i = 0; i < mappers_len; i++) {
        if (mappers[i].ines_id == mapper_id) {
            *name = mappers[i].name;
            return mappers[i].init_func;
        }
    }
    return false;
}

void mapper_init(Machine *vm) {
    Cartridge *cart = vm->cart;
    memset(cart->prg_banks, 0, sizeof(cart->prg_banks));
    memset(cart->chr_banks, 0, sizeof(cart->chr_banks));
    cart->prg_bank_size = SIZE_PRG_ROM;
    cart->chr_bank_size = SIZE_CHR_ROM;
    cart->sram_enabled = true;
    cart->sram_bank = 0;
    
    for (int i = 0; i < mappers_len; i++) {
        if (mappers[i].ines_id == cart->mapper_id) {
            (*mappers[i].init_func)(vm);
            break;
        }
    }
}
