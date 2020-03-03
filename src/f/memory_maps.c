#include "memory_maps.h"

#include "machine.h"
#include "ppu.h"

static void init_common(MemoryMap *mm, Machine *vm) {
    memset(mm, 0, sizeof(MemoryMap));
    mm->vm = vm;
}

// CPU MEMORY MAP ACCESSES //

static uint8_t read_wram(Machine *vm, int offset) {
    return vm->wram[offset];
}
static void write_wram(Machine *vm, int offset, uint8_t value) {
    vm->wram[offset] = value;
}

static uint8_t read_controllers(Machine *vm, int offset) {
    uint8_t value = vm->cpu_mm->last_read & 0b11100000;
    if (vm->controller_bit[offset] < 8) {
        if (vm->controllers[offset] & (1 << vm->controller_bit[offset]++)) {
            value++;
        }
    }
    if (offset) {
        value |= (!vm->ppu->lightgun_sensor << 3) |
                 (!vm->lightgun_trigger << 4);
    }
    return value;
}

static void write_controller_latch(Machine *vm, int offset, uint8_t value) {
    if (value & 1) {
        vm->controller_bit[0] = vm->controller_bit[1] = 0;
    }
    vm->vs_bank = value & 0b100;
}

// PPU MEMORY MAP ACCESSES //

static uint8_t read_nametables(Machine *vm, int offset) {
    return vm->nt_layout[(unsigned int)offset / SIZE_NAMETABLE]
                        [(unsigned int)offset % SIZE_NAMETABLE];
}
static void write_nametables(Machine *vm, int offset, uint8_t value) {
    vm->nt_layout[(unsigned int)offset / SIZE_NAMETABLE]
                 [(unsigned int)offset % SIZE_NAMETABLE] = value;
}

// PUBLIC FUNCTIONS //

void memory_map_cpu_init(MemoryMap *mm, Machine *vm) {
    init_common(mm, vm);
    mm->addr_mask = 0xFFFF;
    
    // Populate the address map
    // 0000-1FFF: WRAM (2kB, repeated)
    for (int i = 0; i < SIZE_WRAM; i++) {
        mm->addrs[i] = (MemoryAddress) {read_wram, write_wram, i % SIZE_WRAM};
    }
    // 2000-4015: PPU and APU registers, defined by their respective inits
    // 4016-4017: Controller I/O
    mm->addrs[0x4016] = (MemoryAddress)
        {read_controllers, write_controller_latch, 0};
    mm->addrs[0x4017] = (MemoryAddress)
        {read_controllers, NULL, 1}; // Also APU on write
    // 4018-401F: Unused
    // 4020-FFFF: Cartridge I/O, defined by the mapper's init
}

void memory_map_ppu_init(MemoryMap *mm, Machine *vm) {
    init_common(mm, vm);
    mm->addr_mask = 0x3FFF;
    
    // Populate the address map
    // 0000-1FFF: Cartridge I/O, defined by the mapper's init
    // 2000-3EFF: Nametables
    for (int i = 0; i < 0x1EFF; i++) {
        mm->addrs[0x2000 + i] = (MemoryAddress)
            {read_nametables, write_nametables, i % (SIZE_NAMETABLE * 4)};
    }
    // 3F00-3FFF: Palettes, defined by ppu_init()
    // 4000-FFFF: Over the 14 bit range
}

uint8_t mm_read(MemoryMap *mm, uint16_t addr) {
    addr &= mm->addr_mask;
    MemoryAddress *f = &mm->addrs[addr];
    if (f->read_func) {
        mm->last_read = (*f->read_func)(mm->vm, f->offset);
    }
    return mm->last_read;
}
uint16_t mm_read_word(MemoryMap *mm, uint16_t addr) {
    return (uint16_t)mm_read(mm, addr) + ((uint16_t)mm_read(mm, addr + 1) << 8);
}

void mm_write(MemoryMap *mm, uint16_t addr, uint8_t value) {
    addr &= mm->addr_mask;
    MemoryAddress *f = &mm->addrs[addr];
    if (f->write_func) {
        (*f->write_func)(mm->vm, f->offset, value);
    }
}
void mm_write_word(MemoryMap *mm, uint16_t addr, uint16_t value) {
    mm_write(mm, addr, value & 0xff);
    mm_write(mm, addr + 1, value >> 8);
}
