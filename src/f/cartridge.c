#include "cartridge.h"

#include "../cpu/65xx.h"
#include "machine.h"
#include "memory_maps.h"

// GENERIC MAPPER I/O //

static uint8_t read_prg(Machine *vm, uint16_t addr) {
    return vm->cart.prg_banks[(addr >> 13) & 3][addr & 0x1FFF];
}

static uint8_t read_chr(Machine *vm, uint16_t addr) {
    return vm->cart.chr_banks[(addr >> 10) & 7][addr & 0x3FF];
}
static void write_chr(Machine *vm, uint16_t addr, uint8_t value) {
    vm->cart.chr_banks[(addr >> 10) & 7][addr & 0x3FF] = value;
}

static uint8_t read_sram(Machine *vm, uint16_t addr) {
    if (vm->cart.sram_enabled) {
        return vm->cart.sram.data[(addr & 0x1FFF) % vm->cart.sram.size];
    }
    return vm->cpu_mm.last_read;
}
static void write_sram(Machine *vm, uint16_t addr, uint8_t value) {
    if (vm->cart.sram_enabled) {
        vm->cart.sram.data[(addr & 0x1FFF) % vm->cart.sram.size] = value;
    }
}

// SHARED INITIALIZERS //

static void init_sram(Machine *vm, int size) {
    vm->cart.sram.size = size;
    vm->cart.sram.data = malloc(size);
    
    // 6000-7FFF: SRAM (up to 8kB, repeated if less)
    for (int i = 0; i < SIZE_SRAM; i++) {
        vm->cpu_mm.read[0x6000 + i] = read_sram;
        vm->cpu_mm.write[0x6000 + i] = write_sram;
    }
}

static void init_register_prg(Machine *vm, WriteFuncPtr register_func) {
    for (int i = 0; i < SIZE_PRG_ROM; i++) {
        vm->cpu_mm.write[0x8000 + i] = register_func;
    }
}

static void init_register_sram(Machine *vm, WriteFuncPtr register_func) {
    for (int i = 0; i < SIZE_SRAM; i++) {
        vm->cpu_mm.write[0x6000 + i] = register_func;
    }
}

// BANK SELECT //

static void select_prg_full(Cartridge *cart, uint8_t pos) {
    uint8_t *offset = cart->prg_rom.data + ((pos << 15) % cart->prg_rom.size);
    cart->prg_banks[0] = offset;
    cart->prg_banks[1] = offset + SIZE_PRG_BANK;
    cart->prg_banks[2] = offset + SIZE_PRG_BANK * 2;
    cart->prg_banks[3] = offset + SIZE_PRG_BANK * 3;
    for (int i = 0; i < 4; i++) {
        cart->prg_banks[i] = offset;
        offset += SIZE_PRG_BANK;
    }
}

static void select_prg_half(Cartridge *cart, int bank, uint8_t pos) {
    uint8_t *offset = cart->prg_rom.data + ((pos << 14) % cart->prg_rom.size);
    bank <<= 1;
    cart->prg_banks[bank] = offset;
    cart->prg_banks[bank + 1] = offset + SIZE_PRG_BANK;
}

static void select_prg_quarter(Cartridge *cart, int bank, uint8_t pos) {
    cart->prg_banks[bank] = cart->prg_rom.data +
                            ((pos << 13) % cart->prg_rom.size);
}

static void select_chr_full(Cartridge *cart, uint8_t pos) {
    uint8_t *offset = cart->chr_memory.data +
                      ((pos << 13) % cart->chr_memory.size);
    cart->chr_banks[0] = offset;
    cart->chr_banks[1] = offset + SIZE_CHR_BANK;
    cart->chr_banks[2] = offset + SIZE_CHR_BANK * 2;
    cart->chr_banks[3] = offset + SIZE_CHR_BANK * 3;
    cart->chr_banks[4] = offset + SIZE_CHR_BANK * 4;
    cart->chr_banks[5] = offset + SIZE_CHR_BANK * 5;
    cart->chr_banks[6] = offset + SIZE_CHR_BANK * 6;
    cart->chr_banks[7] = offset + SIZE_CHR_BANK * 7;
}

static void select_chr_half(Cartridge *cart, int bank, uint8_t pos) {
    uint8_t *offset = cart->chr_memory.data +
                      ((pos << 12) % cart->chr_memory.size);
    bank <<= 2;
    cart->chr_banks[bank] = offset;
    cart->chr_banks[bank + 1] = offset + SIZE_CHR_BANK;
    cart->chr_banks[bank + 2] = offset + SIZE_CHR_BANK * 2;
    cart->chr_banks[bank + 3] = offset + SIZE_CHR_BANK * 3;
}

static void select_chr_quarter(Cartridge *cart, int bank, uint8_t pos) {
    uint8_t *offset = cart->chr_memory.data +
                      ((pos << 11) % cart->chr_memory.size);
    bank <<= 1;
    cart->chr_banks[bank] = offset;
    cart->chr_banks[bank + 1] = offset + SIZE_CHR_BANK;
}

static void select_chr_eighth(Cartridge *cart, int bank, uint8_t pos) {
    cart->chr_banks[bank] = cart->chr_memory.data +
                            ((pos << 10) % cart->chr_memory.size);
}

static int get_prg_last_half(Cartridge *cart, uint8_t pos) {
    return (int)cart->prg_rom.size / (SIZE_PRG_BANK * 2) - pos;
}

static int get_prg_last_quarter(Cartridge *cart, uint8_t pos) {
    return (int)cart->prg_rom.size / SIZE_PRG_BANK - pos;
}

// MAPPER 0: Nintendo NROM (32f/8f, aka. no mapper) //

static void NROM_init(Machine *vm) {
    // Used by Family BASIC only
    if (vm->cart.has_battery_backup) {
        init_sram(vm, SIZE_SRAM / 2);
    }
}

// MAPPER   1: Nintendo MMC1 (variable banking, A/B/H/V control) //
//        155: Nintendo MMC1A (no SRAM protect toggle)           //

static void MMC1_update_prg_banks(Cartridge *cart) {
    if (!BIT_CHECK(cart->mapper.mmc1.ctrl_flags, 3)) {
        select_prg_full(cart, cart->mapper.mmc1.prg_bank >> 1);
    } else if (!BIT_CHECK(cart->mapper.mmc1.ctrl_flags, 2)) {
        select_prg_half(cart, 0, 0);
        select_prg_half(cart, 1, cart->mapper.mmc1.prg_bank);
    } else {
        select_prg_half(cart, 0, cart->mapper.mmc1.prg_bank);
        select_prg_half(cart, 1, get_prg_last_half(cart, 1));
    }
}

static void MMC1_update_chr_banks(Cartridge *cart) {
    if (BIT_CHECK(cart->mapper.mmc1.ctrl_flags, 4)) {
        select_chr_half(cart, 0, cart->mapper.mmc1.chr_banks[0]);
        select_chr_half(cart, 1, cart->mapper.mmc1.chr_banks[1]);
    } else {
        select_chr_full(cart, cart->mapper.mmc1.chr_banks[0] >> 1);
    }
}

static void MMC1_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    MMC1State *mmc1 = &vm->cart.mapper.mmc1;
    if (value & (1 << 7)) {
        mmc1->shift_reg = mmc1->shift_pos = 0;
        return;
    }
    mmc1->shift_reg |= (value & 1) << mmc1->shift_pos;
    mmc1->shift_pos++;
    if (mmc1->shift_pos < 5) {
        return;
    }
    switch ((addr >> 13) & 3) {
        case 0: // 8000-9FFF: Control
            mmc1->ctrl_flags = mmc1->shift_reg;
            machine_set_nt_mirroring(vm, mmc1->ctrl_flags & 3);
            MMC1_update_prg_banks(&vm->cart);
            MMC1_update_chr_banks(&vm->cart);
            break;
        case 3: // E000-FFFF: PRG bank + SRAM write protect
            mmc1->prg_bank = mmc1->shift_reg & 0b1111;
            if (!mmc1->is_a) {
                vm->cart.sram_enabled = !(mmc1->shift_reg & (1 << 4));
            }
            MMC1_update_prg_banks(&vm->cart);
            break;
        default: // A000-DFFF: CHR banks 0, 1
            mmc1->chr_banks[(addr >> 14) & 1] = mmc1->shift_reg;
            MMC1_update_chr_banks(&vm->cart);
    }
    mmc1->shift_reg = mmc1->shift_pos = 0;
}

static void MMC1_init(Machine *vm) {
    Cartridge *cart = &vm->cart;
    memset(&cart->mapper.mmc1, 0, sizeof(MMC1State));
    
    // Booting in 16b+16f PRG mode seems to be the most compatible
    cart->mapper.mmc1.ctrl_flags = 3 << 2;
    MMC1_update_prg_banks(cart);
    
    init_register_prg(vm, MMC1_write_register);
    init_sram(vm, SIZE_SRAM);
}

static void MMC1A_init(Machine *vm) {
    MMC1_init(vm);
    vm->cart.mapper.mmc1.is_a = true;
}

// MAPPER   2: Nintendo UxROM (16b+16f/8f)                              //
//         93: Sunsoft-2 IC on Sunsoft-3R board (register shift 4 bits) //
//         94: Nintendo UN1ROM (register shift 2 bits)                  //
//        180: Nintendo UNROM with 74HC08 (16f+16b/8f)                  //

static void UxROM_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    select_prg_half(&vm->cart, vm->cart.mapper.uxrom.target_bank,
                    value >> vm->cart.mapper.uxrom.bit_offset);
}

static void UxROM_init(Machine *vm) {
    Cartridge *cart = &vm->cart;
    memset(&cart->mapper.uxrom, 0, sizeof(UxROMVariants));
    init_register_prg(vm, UxROM_write_register);
    select_prg_half(cart, 1, get_prg_last_half(cart, 1));
}

static void Sunsoft2R_init(Machine *vm) {
    UxROM_init(vm);
    vm->cart.mapper.uxrom.bit_offset = 4;
}

static void UN1ROM_init(Machine *vm) {
    UxROM_init(vm);
    vm->cart.mapper.uxrom.bit_offset = 2;
}

static void UNROM08_init(Machine *vm) {
    Cartridge *cart = &vm->cart;
    cart->mapper.uxrom.bit_offset = 0;
    cart->mapper.uxrom.target_bank = 1;
    init_register_prg(vm, UxROM_write_register);
}

// MAPPER 3: Nintendo CNROM (32f/8b) //

static void CNROM_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    select_chr_full(&vm->cart, value);
}

static void CNROM_init(Machine *vm) {
    init_register_prg(vm, CNROM_write_register);
}

// MAPPER   4: Nintendo MMC3 and MMC6                                       //
//             (variable banking, H/V control, scanline counter)            //
//        119: TQROM variant (uses both CHR ROM and CHR RAM simultaneously) //

static void MMC3_update_banks(Cartridge *cart) {
    MMC3State *mmc = &cart->mapper.mmc3;
    
    // PRG ROM
    select_prg_quarter(cart, 1, mmc->banks[7]);
    if (BIT_CHECK(mmc->bank_select, 6)) {
        select_prg_quarter(cart, 0, get_prg_last_quarter(cart, 2));
        select_prg_quarter(cart, 2, mmc->banks[6]);
    } else {
        select_prg_quarter(cart, 0, mmc->banks[6]);
        select_prg_quarter(cart, 2, get_prg_last_quarter(cart, 2));
    }
    
    // CHR ROM
    if (BIT_CHECK(mmc->bank_select, 7)) {
        select_chr_eighth(cart, 4, mmc->banks[0]);
        select_chr_eighth(cart, 5, mmc->banks[0] + 1);
        select_chr_eighth(cart, 6, mmc->banks[1]);
        select_chr_eighth(cart, 7, mmc->banks[1] + 1);
        select_chr_eighth(cart, 0, mmc->banks[2]);
        select_chr_eighth(cart, 1, mmc->banks[3]);
        select_chr_eighth(cart, 2, mmc->banks[4]);
        select_chr_eighth(cart, 3, mmc->banks[5]);
    } else {
        select_chr_eighth(cart, 0, mmc->banks[0]);
        select_chr_eighth(cart, 1, mmc->banks[0] + 1);
        select_chr_eighth(cart, 2, mmc->banks[1]);
        select_chr_eighth(cart, 3, mmc->banks[1] + 1);
        select_chr_eighth(cart, 4, mmc->banks[2]);
        select_chr_eighth(cart, 5, mmc->banks[3]);
        select_chr_eighth(cart, 6, mmc->banks[4]);
        select_chr_eighth(cart, 7, mmc->banks[5]);
    }
}

static void MMC3_write_register_bank_select(Machine *vm, uint16_t addr,
                                            uint8_t value) {
    vm->cart.mapper.mmc3.bank_select = value;
    MMC3_update_banks(&vm->cart);
}

static void MMC3_write_register_bank_data(Machine *vm, uint16_t addr,
                                          uint8_t value) {
    Cartridge *cart = &vm->cart;
    int bank = cart->mapper.mmc3.bank_select & 7;
    if (bank < 2) {
        value &= ~1;
    }
    cart->mapper.mmc3.banks[bank] = value;
    MMC3_update_banks(cart);
}

static void MMC3_write_register_mirroring(Machine *vm, uint16_t addr,
                                          uint8_t value) {
    machine_set_nt_mirroring(vm, (value & 1 ? NT_HORIZONTAL : NT_VERTICAL));
}

static void MMC3_write_register_irq_latch(Machine *vm, uint16_t addr,
                                          uint8_t value) {
    vm->cart.mapper.mmc3.irq_latch = value;
}

static void MMC3_write_register_irq_reload(Machine *vm, uint16_t addr,
                                            uint8_t value) {
    vm->cart.mapper.mmc3.irq_counter = 0;
}

static void MMC3_write_register_irq_disable(Machine *vm, uint16_t addr,
                                            uint8_t value) {
    vm->cart.mapper.mmc3.irq_enabled = false;
    BIT_CLEAR(vm->cpu.irq, IRQ_MAPPER);
}

static void MMC3_write_register_irq_enable(Machine *vm, uint16_t addr,
                                           uint8_t value) {
    vm->cart.mapper.mmc3.irq_enabled = true;
}

static uint8_t MMC3_read_chr(Machine *vm, uint16_t addr) {
    MMC3State *mmc = &vm->cart.mapper.mmc3;
    bool current_pt = addr & (1 << 12);
    if (!mmc->last_pt && current_pt) {
        if (mmc->irq_counter) {
            mmc->irq_counter--;
        } else {
            mmc->irq_counter = mmc->irq_latch;
        }
        BIT_SET_IF(vm->cpu.irq, IRQ_MAPPER,
                   !mmc->irq_counter && mmc->irq_enabled);
    }
    mmc->last_pt = current_pt;
    
    return read_chr(vm, addr);
}

static void MMC3_init(Machine *vm) {
    Cartridge *cart = &vm->cart;
    memset(&cart->mapper.mmc3, 0, sizeof(MMC3State));
    
    select_prg_quarter(cart, 3, get_prg_last_quarter(cart, 1));
    MMC3_update_banks(cart);
    
    int i = 0x8000;
    while (i < 0xA000) {
        vm->cpu_mm.write[i++] = MMC3_write_register_bank_select;
        vm->cpu_mm.write[i++] = MMC3_write_register_bank_data;
    }
    while (i < 0xC000) {
        vm->cpu_mm.write[i++] = MMC3_write_register_mirroring;
        i++;    // SRAM protect, intentionally not implemented to ensure
                // cross-compatibility with MMC6 which shares the same mapper ID
    }
    while (i < 0xE000) {
        vm->cpu_mm.write[i++] = MMC3_write_register_irq_latch;
        vm->cpu_mm.write[i++] = MMC3_write_register_irq_reload;
    }
    while (i < 0x10000) {
        vm->cpu_mm.write[i++] = MMC3_write_register_irq_disable;
        vm->cpu_mm.write[i++] = MMC3_write_register_irq_enable;
    }
    
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        vm->ppu_mm.read[i] = MMC3_read_chr;
    }
    
    init_sram(vm, SIZE_SRAM);
}

static void MMC3Q_init(Machine *vm) {
    Cartridge *cart = &vm->cart;
    
    // Change the CHR to RAM and grow it to 128kB
    blob chr_rom = cart->chr_memory;
    cart->chr_is_ram = true;
    cart->chr_memory.size = 16 * SIZE_CHR_ROM;
    cart->chr_memory.data = malloc(cart->chr_memory.size);
    memset(cart->chr_memory.data, 0, cart->chr_memory.size);
    memcpy(cart->chr_memory.data, chr_rom.data, chr_rom.size);
    
    MMC3_init(vm);
}

// MAPPER 7: Nintendo AxROM (32b/8f, A/B control) //

static void AxROM_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    select_prg_full(&vm->cart, value & 0b111);
    machine_set_nt_mirroring(vm, (value & 0b10000 ? NT_SINGLE_B : NT_SINGLE_A));
}

static void AxROM_init(Machine *vm) {
    machine_set_nt_mirroring(vm, NT_SINGLE_A);
    init_register_prg(vm, AxROM_write_register);
}

// MAPPER  9: Nintendo MMC2                                        //
//            (8b+24f/4b+4b, CHR bank read trigger, H/V control)   //
//        10: Nintendo MMC4                                        //
//            (similar but 16b+16f/4b+4b and simpler read trigger) //

static void MMC24_update_chr_banks(Cartridge *cart) {
    for (int i = 0; i < 2; i++) {
        select_chr_half(cart, i,
            cart->mapper.mmc24.chr_banks[i][cart->mapper.mmc24.chr_latches[i]]);
    }
}

static void MMC2_write_register_prg(Machine *vm, uint16_t addr, uint8_t value) {
    select_prg_quarter(&vm->cart, 0, value);
}

static void MMC4_write_register_prg(Machine *vm, uint16_t addr, uint8_t value) {
    select_prg_half(&vm->cart, 0, value);
}

static void MMC24_write_register_chr(Machine *vm, uint16_t addr,
                                     uint8_t value) {
    int bank = ((addr >> 12) & 7) - 3;
    vm->cart.mapper.mmc24.chr_banks[bank / 2][bank % 2] = value;
    MMC24_update_chr_banks(&vm->cart);
}

static void MMC24_write_register_mirroring(Machine *vm, uint16_t addr,
                                           uint8_t value) {
    machine_set_nt_mirroring(vm, (value & 1 ? NT_HORIZONTAL : NT_VERTICAL));
}

static uint8_t MMC24_read_chr(Machine *vm, uint16_t addr) {
    uint8_t value = read_chr(vm, addr);
    
    Cartridge *cart = &vm->cart;
    MMC24State *mmc = &cart->mapper.mmc24;
    
    bool bank = BIT_CHECK(addr, 12);
    addr &= 0xFFF;
    if (mmc->is_2 && bank) {
        if (addr >= 0xFD8 && addr <= 0xFDF) {
            mmc->chr_latches[bank] = 0;
            MMC24_update_chr_banks(cart);
        } else if (addr >= 0xFE8 && addr <= 0xFEF) {
            mmc->chr_latches[bank] = 1;
            MMC24_update_chr_banks(cart);
        }
    } else {
        if (addr == 0xFD8) {
            mmc->chr_latches[bank] = 0;
            MMC24_update_chr_banks(cart);
        } else if (addr == 0xFE8) {
            mmc->chr_latches[bank] = 1;
            MMC24_update_chr_banks(cart);
        }
    }
    
    return value;
}

static void MMC24_init_common(Machine *vm, WriteFuncPtr register_prg_func) {
    memset(&vm->cart.mapper.mmc24, 0, sizeof(MMC24State));
    
    int i = 0xA000;
    while (i < 0xB000) {
        vm->cpu_mm.write[i++] = register_prg_func;
    }
    while (i < 0xF000) {
        vm->cpu_mm.write[i++] = MMC24_write_register_chr;
    }
    while (i < 0x10000) {
        vm->cpu_mm.write[i++] = MMC24_write_register_mirroring;
    }
    
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        vm->ppu_mm.read[i] = MMC24_read_chr;
    }
}

static void MMC2_init(Machine *vm) {
    MMC24_init_common(vm, MMC2_write_register_prg);

    Cartridge *cart = &vm->cart;
    cart->mapper.mmc24.is_2 = true;
    
    // Last three banks are fixed to the end
    const int last = get_prg_last_quarter(cart, 1);
    select_prg_quarter(cart, 1, last - 2);
    select_prg_quarter(cart, 2, last - 1);
    select_prg_quarter(cart, 3, last);
}

static void MMC4_init(Machine *vm) {
    MMC24_init_common(vm, MMC4_write_register_prg);
    
    Cartridge *cart = &vm->cart;
    select_prg_half(cart, 1, get_prg_last_half(cart, 1));
    
    init_sram(vm, SIZE_SRAM);
}

// MAPPER 11: Color Dreams (32b/8b, similar to GxROM but reversed register) //

static void Color_Dreams_write_register(Machine *vm, uint16_t addr,
                                        uint8_t value) {
    select_prg_full(&vm->cart, value & 0xF);
    select_chr_full(&vm->cart, value >> 4);
}

static void Color_Dreams_init(Machine *vm) {
    init_register_prg(vm, Color_Dreams_write_register);
}

// MAPPER 13: Nintendo CPROM (32f/4f+4b, 16kB CHR RAM) //

static void CPROM_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    select_chr_half(&vm->cart, 1, value);
}

static void CPROM_init(Machine *vm) {
    Cartridge *cart = &vm->cart;
    
    // Force CHR RAM and expand it to 16kB
    cart->chr_memory.size = SIZE_CHR_ROM * 2;
    if (cart->chr_is_ram) {
        cart->chr_memory.data = realloc(cart->chr_memory.data,
                                        cart->chr_memory.size);
    } else {
        cart->chr_memory.data = malloc(cart->chr_memory.size);
    }
    cart->chr_is_ram = true;

    init_register_prg(vm, CPROM_write_register);
}

// MAPPER 34: Nintendo BNROM (32b/8f)  //
//        39: Unnamed Subor equivalent //

static void BNROM_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    select_prg_full(&vm->cart, value);
}

static void BNROM_init(Machine *vm) {
    init_register_prg(vm, BNROM_write_register);
}

// MAPPER 38: PCI556 (32b/8b) //

static void PCI556_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    select_prg_full(&vm->cart, value & 7);
    select_chr_full(&vm->cart, value >> 2);
}

static void PCI556_init(Machine *vm) {
    // Register is only in the upper half of the SRAM area
    for (int i = 0x7000; i < 0x8000; i++) {
        vm->cpu_mm.write[i] = PCI556_write_register;
    }
}

// MAPPER  66: Nintendo GNROM and MHROM (32b/8b)                          //
//        140: Jaleco JF-11/14 (similar but register in the SRAM area)    //

static void GxROM_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    select_prg_full(&vm->cart, value >> 4);
    select_chr_full(&vm->cart, value & 0xF);
}

static void GxROM_init(Machine *vm) {
    init_register_prg(vm, GxROM_write_register);
}

static void JF1114_init(Machine *vm) {
    init_register_sram(vm, GxROM_write_register);
}

// MAPPER 68: Sunsoft-4 (16b+16f/2b+2b+2b+2b, NT mapped to CHR) //

static void Sunsoft4_update_nametables(Machine *vm) {
    const int layouts[] = {
        0, 1, 0, 1, // VERTICAL
        0, 0, 1, 1, // HORIZONTAL
        0, 0, 0, 0, // SINGLE_A
        1, 1, 1, 1, // SINGLE_B
    };
    Cartridge *cart = &vm->cart;
    uint8_t *nts[] = {vm->nametables[0], vm->nametables[1]};
    const int *layout = layouts + (cart->mapper.sunsoft4.ctrl & 0b11) * 4;
    uint8_t **memory = (BIT_CHECK(cart->mapper.sunsoft4.ctrl, 4) ?
                        cart->mapper.sunsoft4.chr_nt_banks : nts);
    for (int i = 0; i < 4; i++) {
        vm->nt_layout[i] = memory[layout[i]];
    }
}

static void Sunsoft4_write_register_chr(Machine *vm, uint16_t addr,
                                        uint8_t value) {
    select_chr_quarter(&vm->cart, (addr >> 12) & 3, value);
}

static void Sunsoft4_write_register_nt(Machine *vm, uint16_t addr,
                                       uint8_t value) {
    Cartridge *cart = &vm->cart;
    vm->cart.mapper.sunsoft4.chr_nt_banks[(addr >> 12) & 1] =
        cart->chr_memory.data +
        (((value | 0x80) << 10) % cart->chr_memory.size);
    Sunsoft4_update_nametables(vm);
}

static void Sunsoft4_write_register_ctrl(Machine *vm, uint16_t addr,
                                         uint8_t value) {
    vm->cart.mapper.sunsoft4.ctrl = value;
    Sunsoft4_update_nametables(vm);
}

static void Sunsoft4_write_register_prg(Machine *vm, uint16_t addr,
                                        uint8_t value) {
    select_prg_half(&vm->cart, 0, value & 0xF);
    // TODO: bit 4 enable SRAM?
}

static void Sunsoft4_write_nametables(Machine *vm, uint16_t addr,
                                      uint8_t value) {
    if (!BIT_CHECK(vm->cart.mapper.sunsoft4.ctrl, 4)) {
        vm->nt_layout[(addr >> 10) & 0b11][addr & 0x3FF] = value;
    }
}

static void Sunsoft4_init(Machine *vm) {
    Cartridge *cart = &vm->cart;
    memset(&cart->mapper.sunsoft4, 0, sizeof(Sunsoft4State));
    
    for (int i = 0x8000; i < 0xC000; i++) {
        vm->cpu_mm.write[i] = Sunsoft4_write_register_chr;
    }
    for (int i = 0xC000; i < 0xE000; i++) {
        vm->cpu_mm.write[i] = Sunsoft4_write_register_nt;
    }
    for (int i = 0xE000; i < 0xF000; i++) {
        vm->cpu_mm.write[i] = Sunsoft4_write_register_ctrl;
    }
    for (int i = 0xF000; i < 0x10000; i++) {
        vm->cpu_mm.write[i] = Sunsoft4_write_register_prg;
    }
    
    init_sram(vm, SIZE_SRAM);
    
    select_prg_half(cart, 1, get_prg_last_half(cart, 1));
    
    // Need to enforce write protection when CHR ROM is mapped to NT
    for (int i = 0; i < 0x1EFF; i++) {
        vm->ppu_mm.write[0x2000 + i] = Sunsoft4_write_nametables;
    }
}

// MAPPER  70: Bandai 74*161/161/32 (16b+16f/8b with equivalent register) //
//        152: Bandai 74*161/161/32 single screen (70 with A/B control)   //

static void Bandai74_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    select_prg_half(&vm->cart, 0, (value >> 4) & 7);
    select_chr_full(&vm->cart, value & 0xF);
}

static void Bandai74s_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    Bandai74_write_register(vm, addr, value);
    machine_set_nt_mirroring(vm, (BIT_CHECK(value, 7) ? NT_SINGLE_B
                                                      : NT_SINGLE_A));
}

static void Bandai74_init_common(Machine *vm, WriteFuncPtr register_func) {
    Cartridge *cart = &vm->cart;
    select_prg_half(cart, 1, get_prg_last_half(cart, 1));
    init_register_prg(vm, register_func);
}

static void Bandai74_init(Machine *vm) {
    Bandai74_init_common(vm, Bandai74_write_register);
}

static void Bandai74s_init(Machine *vm) {
    Bandai74_init_common(vm, Bandai74s_write_register);
    machine_set_nt_mirroring(vm, NT_SINGLE_A);
}

// MAPPER  75: Konami VRC1 (8b+8b+8b+8f/4b+4b, H/V control) //
//        151: Duplicate (intended for Vs. System)          //

static void VRC1_update_chr_banks(Cartridge *cart) {
    select_chr_half(cart, 0, cart->mapper.vrc1_chr_banks[0]);
    select_chr_half(cart, 1, cart->mapper.vrc1_chr_banks[1]);
}

static void VRC1_write_register_prg(Machine *vm, uint16_t addr, uint8_t value) {
    select_prg_quarter(&vm->cart, (addr >> 13) & 3, value);
}

static void VRC1_write_register_misc(Machine *vm, uint16_t addr,
                                     uint8_t value) {
    machine_set_nt_mirroring(vm, (BIT_CHECK(value, 0) ? NT_HORIZONTAL
                                                      : NT_VERTICAL));
    uint8_t *banks = vm->cart.mapper.vrc1_chr_banks;
    banks[0] = (banks[0] & 0xF) | (!!BIT_CHECK(value, 1) << 4);
    banks[1] = (banks[1] & 0xF) | (!!BIT_CHECK(value, 2) << 4);
    VRC1_update_chr_banks(&vm->cart);
}

static void VRC1_write_register_chr(Machine *vm, uint16_t addr, uint8_t value) {
    uint8_t *bank = vm->cart.mapper.vrc1_chr_banks + ((addr >> 12) & 1);
    *bank = (*bank & 0x10) | (value & 0xF);
}

static void VRC1_init(Machine *vm) {
    Cartridge *cart = &vm->cart;
    select_prg_quarter(cart, 3, get_prg_last_quarter(cart, 1));
    cart->mapper.vrc1_chr_banks[0] = cart->mapper.vrc1_chr_banks[1] = 0;
    
    for (int i = 0x8000; i < 0xE000; i += 0x2000) {
        for (int j = 0; j < 0x1000; j++) {
            vm->cpu_mm.write[i + j] = VRC1_write_register_prg;
        }
    }
    for (int i = 0x9000; i < 0xA000; i++) {
        vm->cpu_mm.write[i] = VRC1_write_register_misc;
    }
    for (int i = 0xE000; i < 0x10000; i++) {
        vm->cpu_mm.write[i] = VRC1_write_register_chr;
    }
}

// MAPPER  79: American Video Entertainment NINA-03/06 (32b/8b)      //
//        113: Multicart variant (larger bank capacity, H/V control) //
//        146: Duplicate of 79                                       //

static void NINA0306_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    select_prg_full(&vm->cart, value >> 3);
    select_chr_full(&vm->cart, value & 7);
}

static void NINA0306MC_write_register(Machine *vm, uint16_t addr,
                                      uint8_t value) {
    select_prg_full(&vm->cart, (value >> 3) & 7);
    select_chr_full(&vm->cart, ((value >> 3) & 8) | (value & 7));
    machine_set_nt_mirroring(vm, (BIT_CHECK(value, 7) ? NT_VERTICAL
                                                      : NT_HORIZONTAL));
}

static void NINA0306_init_register(Machine *vm, WriteFuncPtr register_func) {
    // The register is at a more complicated location but who cares
    for (int i = 0x4100; i < 0x6000; i++) {
        vm->cpu_mm.write[i] = register_func;
    }
}

static void NINA0306_init(Machine *vm) {
    NINA0306_init_register(vm, NINA0306_write_register);
}

static void NINA0306MC_init(Machine *vm) {
    NINA0306_init_register(vm, NINA0306MC_write_register);
}

// MAPPER 87: Konami/Jaleco/Taito 74*139/74       //
//            (32f/8b, reversed bits in register) //

static void KJT74_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    select_chr_full(&vm->cart, ((value & 1) << 1) | ((value & 2) >> 1));
}

static void KJT74_init(Machine *vm) {
    init_register_sram(vm, KJT74_write_register);
}

// MAPPER 89: Sunsoft-2 IC on Sunsoft-3 board (16b+16f/8b, A/B control) //

static void Sunsoft2_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    select_prg_half(&vm->cart, 0, (addr >> 4) & 7);
    select_chr_full(&vm->cart, ((addr >> 4) & 8) | (addr & 7));
    machine_set_nt_mirroring(vm, (BIT_CHECK(value, 3) ? NT_SINGLE_B
                                                      : NT_SINGLE_A));
}

static void Sunsoft2_init(Machine *vm) {
    Cartridge *cart = &vm->cart;
    select_prg_half(cart, 1, get_prg_last_half(cart, 1));
    init_register_prg(vm, Sunsoft2_write_register);
    machine_set_nt_mirroring(vm, NT_SINGLE_A);
}

// MAPPER 97: Irem TAM-S1 (16f+16b/8f, A/B/H/V control) //

static void TAMS1_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    select_prg_half(&vm->cart, 1, value & 0x1F);
    machine_set_nt_mirroring(vm, BIT_CHECK(value, 7) ? NT_VERTICAL
                                                     : NT_HORIZONTAL);
}

static void TAMS1_init(Machine *vm) {
    Cartridge *cart = &vm->cart;
    init_register_prg(vm, TAMS1_write_register);
    machine_set_nt_mirroring(vm, NT_SINGLE_A);
    
    // This is not a typo, it really fixes the *first* bank to the end
    select_prg_half(cart, 0, get_prg_last_half(cart, 1));
}

// MAPPER 99: Nintendo Vs. System default board (8b+24f/8b via $4016 bit 2) //

static void VS_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    bool selected = BIT_CHECK(value, 2);
    select_prg_quarter(&vm->cart, 0, selected << 2);
    select_chr_full(&vm->cart, selected);
    (*vm->cart.mapper.hijacked_reg)(vm, addr, value);
}

static void VS_init(Machine *vm) {
    Cartridge *cart = &vm->cart;
    
    cart->mapper.hijacked_reg = vm->cpu_mm.write[0x4016];
    vm->cpu_mm.write[0x4016] = VS_write_register;
    
    init_sram(vm, SIZE_SRAM);
}

// MAPPER 184: Sunsoft-1 (32f/4b+4b) //

static void Sunsoft1_write_register(Machine *vm, uint16_t addr, uint8_t value) {
    select_chr_half(&vm->cart, 0, value & 7);
    select_chr_half(&vm->cart, 1, 4 | ((value >> 4) & 3));
}

static void Sunsoft1_init(Machine *vm) {
    select_chr_half(&vm->cart, 1, 4);
    init_register_sram(vm, Sunsoft1_write_register);
}

// MAPPER 185: Nintendo CNROM, but abused as a crude copy protection scheme //
//             (all games are really just 32f/8f)                           //

static uint8_t CNROM_CP_read_chr(Machine *vm, uint16_t addr) {
    const uint8_t dummies[] = {0xBE, 0xEF};
    if (vm->cart.mapper.cp_counter < 2) {
        return dummies[vm->cart.mapper.cp_counter++];
    }
    return read_chr(vm, addr);
}

static void CNROM_CP_init(Machine *vm) {
    vm->cart.mapper.cp_counter = 0;
    
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        vm->ppu_mm.read[i] = CNROM_CP_read_chr;
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

void mapper_init(Machine *vm, int mapper_id) {
    Cartridge *cart = &vm->cart;
    cart->sram_enabled = true;
    
    // Initialize banks to first ranges
    for (int i = 0; i < 4; i++) {
        cart->prg_banks[i] = cart->prg_rom.data +
                             ((SIZE_PRG_BANK * i) % cart->prg_rom.size);
    }
    for (int i = 0; i < 8; i++) {
        cart->chr_banks[i] = cart->chr_memory.data + SIZE_CHR_BANK * i;
    }
    
    // CPU 8000-FFFF: PRG ROM (32kB, repeated if 16kB)
    for (int i = 0; i < SIZE_PRG_ROM; i++) {
        vm->cpu_mm.read[0x8000 + i] = read_prg;
    }
    
    // PPU 0000-1FFF: CHR ROM (8kB)
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        vm->ppu_mm.read[i] = read_chr;
    }
    
    for (int i = 0; i < mappers_len; i++) {
        if (mappers[i].ines_id == mapper_id) {
            (*mappers[i].init_func)(vm);
            break;
        }
    }
    
    if (cart->chr_is_ram) {
        for (int i = 0; i < SIZE_CHR_ROM; i++) {
            vm->ppu_mm.write[i] = write_chr;
        }
    }
}
