#include "cartridge.h"

#include "memory_maps.h"

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
    for (int i = 0; i < SIZE_PRG_ROM; i++) {
        mm->addrs[0x8000 + i] = (MemoryAddress)
            {generic_read_prg, NULL, i % mm->cart->prg_rom_size};
    }
}

static void generic_init_ppu(MemoryMap *mm) {
    for (int i = 0; i < 0x2000; i++) {
        mm->addrs[i] = (MemoryAddress) {generic_read_chr,
            (mm->cart->chr_is_ram ? generic_write_chr : NULL), i};
    }
}

// MAPPER 0: NROM //

static void NROM_init(Cartridge *cart, MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    generic_init_cpu(cpu_mm);
    generic_init_ppu(ppu_mm);
}

// MAPPER 2: UxROM //

static uint8_t UxROM_read_banked_prg(MemoryMap *mm, int offset) {
    return mm->cart->prg_rom[mm->cart->mapper.uxrom_bank * 0x4000 + offset];
}

static void UxROM_write_bank_select(MemoryMap *mm, int offset, uint8_t value) {
    mm->cart->mapper.uxrom_bank = value;
}

static void UxROM_init(Cartridge *cart, MemoryMap *cpu_mm, MemoryMap *ppu_mm) {
    cart->mapper.uxrom_bank = 0;
    
    const int last_bank = cart->prg_rom_size - SIZE_PRG_ROM / 2;
    for (int i = 0; i < SIZE_PRG_ROM / 2; i++) {
        cpu_mm->addrs[0x8000 + i] = (MemoryAddress)
            {UxROM_read_banked_prg, UxROM_write_bank_select, i};
        cpu_mm->addrs[0x8000 + SIZE_PRG_ROM / 2 + i] = (MemoryAddress)
            {generic_read_prg, UxROM_write_bank_select, last_bank + i};
    }

    generic_init_ppu(ppu_mm);
}

// MAPPER ENUMERATION ARRAY //

static const MapperInfo mappers[] = {
    {0, "NROM", NROM_init},
    {2, "UxROM", UxROM_init},
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
            (*mappers[i].init_func)(cart, cpu_mm, ppu_mm);
            return true;
        }
    }
    return false;
}
