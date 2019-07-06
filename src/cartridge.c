#include "cartridge.h"

#include "memory_maps.h"
#include "ppu.h"

// GENERIC MAPPER I/O //

static uint8_t read_fixed_prg(MemoryMap *mm, int offset) {
    return mm->cart->prg_rom[offset];
}
static uint8_t read_banked_prg(MemoryMap *mm, int offset) {
    return mm->cart->prg_rom[mm->cart->prg_bank * mm->cart->prg_bank_size +                          offset];
}

static uint8_t read_fixed_chr(MemoryMap *mm, int offset) {
    return mm->cart->chr_memory[offset];
}
static uint8_t read_banked_chr(MemoryMap *mm, int offset) {
    return mm->cart->chr_memory[mm->cart->chr_bank * mm->cart->chr_bank_size +
                                offset];
}

static void write_fixed_chr(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->chr_memory[offset] = value;
}
static void write_banked_chr(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->chr_memory[mm->cart->chr_bank * mm->cart->chr_bank_size +
                         offset] = value;
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

// MAPPER 0: NROM (aka. no mapper) //

static void NROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    init_fixed_prg(cpu_mm, NULL);
    init_fixed_chr(ppu_mm);
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
                            (value & 0b10000 ? SINGLE_B : SINGLE_A));
}

static void AxROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    mm_ppu_set_nt_mirroring(&cpu_mm->data.cpu.ppu->mm->data.ppu, SINGLE_A);
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
    cart->prg_bank = cart->chr_bank = 0;
    cart->prg_bank_size = SIZE_PRG_ROM;
    cart->chr_bank_size = SIZE_CHR_ROM;
    
    for (int i = 0; i < mappers_len; i++) {
        if (mappers[i].ines_id == cart->mapper_id) {
            (*mappers[i].init_func)(cpu_mm, ppu_mm);
            break;
        }
    }
}
