#include "memory_maps.h"

// ADDRESS I/O //

static uint8_t read_wram(memory_map *mm, int offset) {
    return mm->wram[offset];
}
static void write_wram(memory_map *mm, int offset, uint8_t value) {
    mm->wram[offset] = value;
}

static uint8_t read_prg_rom(memory_map *mm, int offset) {
    return mm->prg_rom[offset];
}

static uint8_t always_vblank(memory_map *mm, int offset) {
    return 0xFF;
}

// PUBLIC FUNCTIONS //

void memory_map_cpu_init(memory_map *mm, const uint8_t *prg_rom) {
    mm->last_read = 0;
    mm->prg_rom = prg_rom;
    
    int i;
    
    // Wipe the WRAM
    for (i = 0; i < SIZE_WRAM; i++) {
        mm->wram[i] = 0;
    }
    
    // Populate the address map
    // 0000-1FFF: WRAM (2kB repeated multiple times)
    for (i = 0; i < 0x2000; i++) {
        mm->addrs[i] = (memory_address) {read_wram, write_wram, i % SIZE_WRAM};
    }
    // 2000-7FFF: TODO, make it all open bus for now
    for (i = 0x2000; i < 0x8000; i++) {
        mm->addrs[i] = (memory_address) {NULL, NULL, 0};
    }
    // 8000-FFFF: PRG ROM (32kB)
    for (i = 0; i < 0x8000; i++) {
        mm->addrs[i + 0x8000] = (memory_address) {read_prg_rom, NULL, i};
    }
    
    // Have $2002 (PPUSTATUS) always return all true for now, so we can go past the vblank check
    mm->addrs[0x2002] = (memory_address) { always_vblank, NULL, 0};
}

uint8_t mm_read(memory_map *mm, uint16_t addr) {
    memory_address *f = &mm->addrs[addr];
    if (f->read_func) {
        mm->last_read = (*f->read_func)(mm, f->offset);
    }
    return mm->last_read;
}
uint16_t mm_read_word(memory_map *mm, uint16_t addr) {
    return (uint16_t)mm_read(mm, addr) + ((uint16_t)mm_read(mm, addr + 1) << 8);
}

void mm_write(memory_map *mm, uint16_t addr, uint8_t value) {
    memory_address *f = &mm->addrs[addr];
    if (f->write_func) {
        (*f->write_func)(mm, f->offset, value);
    }
}
void mm_write_word(memory_map *mm, uint16_t addr, uint16_t value) {
    mm_write(mm, addr, value & 0xff);
    mm_write(mm, addr + 1, value >> 8);
}
