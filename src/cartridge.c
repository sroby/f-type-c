#include "cartridge.h"

#include "memory_maps.h"
#include "ppu.h"

// GENERIC MAPPER I/O //

static uint8_t generic_read_prg(MemoryMap *mm, int offset) {
    return mm->cart->prg_rom[offset];
}

static uint8_t generic_read_chr(MemoryMap *mm, int offset) {
    return mm->cart->chr_memory[offset];
}
static void generic_write_chr(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->chr_memory[offset] = value;
}

static void generic_init_cpu(MemoryMap *mm) {
    // 8000-FFFF: PRG ROM (32kB, repeated if 16kB)
    for (int i = 0; i < SIZE_PRG_ROM; i++) {
        mm->addrs[0x8000 + i] = (MemoryAddress)
            {generic_read_prg, NULL, i % mm->cart->prg_rom_size};
    }
}

static void generic_init_ppu(MemoryMap *mm) {
    // 0000-1FFF: CHR ROM (8kB)
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        mm->addrs[i] = (MemoryAddress) {generic_read_chr,
            (mm->cart->chr_is_ram ? generic_write_chr : NULL), i};
    }
}

// MAPPER 0: NROM (aka. no mapper) //

static void NROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    generic_init_cpu(cpu_mm);
    generic_init_ppu(ppu_mm);
}

// MAPPER 2: UxROM (bank switchable + fixed PRG ROM) //

static uint8_t UxROM_read_banked_prg(MemoryMap *mm, int offset) {
    return mm->cart->prg_rom[mm->cart->mapper.bank * SIZE_PRG_ROM / 2 + offset];
}

static void UxROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    // TODO: Limit maximum page to size of PRG ROM
    mm->cart->mapper.bank = value;
}

static void UxROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    UxROM_write_register(cpu_mm, 0, 0);
    
    const int last_bank = cpu_mm->cart->prg_rom_size - SIZE_PRG_ROM / 2;
    // CPU 8000-BFFF: 16kB switchable bank
    // CPU C000-FFFF: 16kB fixed to the last bank
    for (int i = 0; i < SIZE_PRG_ROM / 2; i++) {
        cpu_mm->addrs[0x8000 + i] = (MemoryAddress)
            {UxROM_read_banked_prg, UxROM_write_register, i};
        cpu_mm->addrs[0x8000 + SIZE_PRG_ROM / 2 + i] = (MemoryAddress)
            {generic_read_prg, UxROM_write_register, last_bank + i};
    }

    generic_init_ppu(ppu_mm);
}

// MAPPER 3: CNROM (bank switchable CHR ROM) //

static void CNROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->mapper.bank = value & 0b11;
}

static uint8_t CNROM_read_banked_chr(MemoryMap *mm, int offset) {
    return mm->cart->chr_memory[mm->cart->mapper.bank * SIZE_CHR_ROM + offset];
}

static void CNROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    CNROM_write_register(cpu_mm, 0, 0);
    
    // CPU 8000-FFFF: Generic but with a register
    generic_init_cpu(cpu_mm);
    for (int i = 0; i < SIZE_PRG_ROM; i++) {
        cpu_mm->addrs[0x8000 + i].write_func = CNROM_write_register;
    }
    
    // PPU 0000-1FFF: Single switchable bank
    for (int i = 0; i < SIZE_CHR_ROM; i++) {
        ppu_mm->addrs[i] = (MemoryAddress) {CNROM_read_banked_chr, NULL, i};
    }
}

// MAPPER 7: AxROM (bank switchable PRG ROM, single nametable toggle) //

static uint8_t AxROM_read_banked_prg(MemoryMap *mm, int offset) {
    return mm->cart->prg_rom[mm->cart->mapper.bank * SIZE_PRG_ROM + offset];
}

static void AxROM_write_register(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->mapper.bank = value & 0b111;
    mm_ppu_set_nt_mirroring(&mm->data.cpu.ppu->mm->data.ppu,
                            (value & 0b10000 ? SINGLE_B : SINGLE_A));
}

static void AxROM_init(MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    AxROM_write_register(cpu_mm, 0, 0);
    
    // CPU 8000-FFFF: Single switchable bank
    for (int i = 0; i < SIZE_PRG_ROM; i++) {
        cpu_mm->addrs[0x8000 + i] = (MemoryAddress)
            {AxROM_read_banked_prg, AxROM_write_register, i};
    }
    
    generic_init_ppu(ppu_mm);
}

// MAPPER ENUMERATION ARRAY //

static const MapperInfo mappers[] = {
    {0, "NROM", NROM_init},
    {2, "UxROM", UxROM_init},
    {3, "CNROM", CNROM_init},
    {7, "AxROM", AxROM_init},
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

bool mapper_init(Cartridge *cart, MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    for (int i = 0; i < mappers_len; i++) {
        if (mappers[i].ines_id == cart->mapper_id) {
            (*mappers[i].init_func)(cpu_mm, ppu_mm);
            return true;
        }
    }
    return false;
}
