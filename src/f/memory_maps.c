#include "memory_maps.h"

#include "../input.h"
#include "machine.h"
#include "ppu.h"

static void init_common(MemoryMap *mm, Machine *vm) {
    memset(mm, 0, sizeof(MemoryMap));
    mm->vm = vm;
}

static void write_open_bus(Machine *vm, uint16_t addr, uint8_t value) {
    // Does nothing, obviously
}

// CPU MEMORY MAP ACCESSES //

static uint8_t read_cpu_open_bus(Machine *vm, uint16_t addr) {
    return vm->cpu_mm.last_read;
}

static uint8_t read_wram(Machine *vm, uint16_t addr) {
    return vm->wram[addr];
}
static void write_wram(Machine *vm, uint16_t addr, uint8_t value) {
    vm->wram[addr] = value;
}

static uint8_t read_controllers(Machine *vm, uint16_t addr) {
    int port = addr & 1;
    uint8_t value = vm->cpu_mm.last_read & 0b11100000;
    value += vm->ctrl_latch[port] & 1;
    vm->ctrl_latch[port] >>= 1;
    if (port) {
        value |= (!vm->ppu.lightgun_sensor << 3) |
                 (!vm->input->lightgun_trigger << 4);
    }
    return value;
}

static void write_controller_latch(Machine *vm, uint16_t addr, uint8_t value) {
    if (value & 1) {
        vm->ctrl_latch[0] = vm->input->controllers[0];
        vm->ctrl_latch[1] = vm->input->controllers[1];
    }
}

// PPU MEMORY MAP ACCESSES //

static uint8_t read_ppu_open_bus(Machine *vm, uint16_t addr) {
    return vm->ppu_mm.last_read;
}

static uint8_t read_nametables(Machine *vm, uint16_t addr) {
    return vm->nt_layout[(addr >> 10) & 0b11][addr & 0x3FF];
}
static void write_nametables(Machine *vm, uint16_t addr, uint8_t value) {
    vm->nt_layout[(addr >> 10) & 0b11][addr & 0x3FF] = value;
}

// PUBLIC FUNCTIONS //

void memory_map_cpu_init(MemoryMap *mm, Machine *vm) {
    init_common(mm, vm);
    mm->addr_mask = 0xFFFF;
    
    for (int i = 0; i < 0x10000; i++) {
        mm->read[i] = read_cpu_open_bus;
        mm->write[i] = write_open_bus;
    }
    
    // Populate the address map
    // 0000-1FFF: WRAM (2kB, repeated)
    for (int i = 0; i < SIZE_WRAM; i++) {
        mm->read[i] = read_wram;
        mm->write[i] = write_wram;
    }
    // 2000-4015: PPU and APU registers, defined by their respective inits
    // 4016-4017: Controller I/O
    mm->read[0x4016] = mm->read[0x4017] = read_controllers;
    mm->write[0x4016] = write_controller_latch;
    // 4018-401F: Test mode registers, not implemented
    // 4020-FFFF: Cartridge I/O, defined by the mapper's init
}

void memory_map_ppu_init(MemoryMap *mm, Machine *vm) {
    init_common(mm, vm);
    mm->addr_mask = 0x3FFF;
    
    for (int i = 0; i < 0x4000; i++) {
        mm->read[i] = read_ppu_open_bus;
        mm->write[i] = write_open_bus;
    }
    
    // Populate the address map
    // 0000-1FFF: Cartridge I/O, defined by the mapper's init
    // 2000-3EFF: Nametables
    for (int i = 0x2000; i < 0x3F00; i++) {
        mm->read[i] = read_nametables;
        mm->write[i] = write_nametables;
    }
    // 3F00-3FFF: Palettes, defined by ppu_init()
    // 4000-FFFF: Over the 14 bit range
}

uint8_t mm_read(MemoryMap *mm, uint16_t addr) {
    addr &= mm->addr_mask;
    mm->last_read = (mm->read[addr])(mm->vm, addr);
    return mm->last_read;
}
uint16_t mm_read_word(MemoryMap *mm, uint16_t addr) {
    return (uint16_t)mm_read(mm, addr) + ((uint16_t)mm_read(mm, addr + 1) << 8);
}

void mm_write(MemoryMap *mm, uint16_t addr, uint8_t value) {
    addr &= mm->addr_mask;
    (mm->write[addr])(mm->vm, addr, value);
}
void mm_write_word(MemoryMap *mm, uint16_t addr, uint16_t value) {
    mm_write(mm, addr, value & 0xff);
    mm_write(mm, addr + 1, value >> 8);
}
