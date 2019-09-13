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

static uint8_t read_ppu_register(Machine *vm, int offset) {
    return ppu_read_register(vm->ppu, offset);
}
static void write_ppu_register(Machine *vm, int offset, uint8_t value) {
    ppu_write_register(vm->ppu, offset, value);
}

static void write_oam_dma(Machine *vm, int offset, uint8_t value) {
    if (value == 0x40) {
        return; // Avoid a (very unlikely) infinite loop
    }
    uint8_t page[0x100];
    uint16_t page_addr = (uint16_t)value << 8;
    for (int i = 0; i < 0x100; i++) {
        page[i] = mm_read(vm->cpu_mm, page_addr + i);
    }
    ppu_write_oam_dma(vm->ppu, page);
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
    return vm->nt_layout[offset / SIZE_NAMETABLE][offset % SIZE_NAMETABLE];
}
static void write_nametables(Machine *vm, int offset, uint8_t value) {
    vm->nt_layout[offset / SIZE_NAMETABLE][offset % SIZE_NAMETABLE] = value;
}

static uint8_t read_background_colors(Machine *vm, int offset) {
    return vm->ppu->background_colors[offset];
}
static void write_background_colors(Machine *vm, int offset, uint8_t value) {
    vm->ppu->background_colors[offset] = value & MASK_COLOR;
}

static uint8_t read_palettes(Machine *vm, int offset) {
    return vm->ppu->palettes[offset];
}
static void write_palettes(Machine *vm, int offset, uint8_t value) {
    vm->ppu->palettes[offset] = value & MASK_COLOR;
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
    // 2000-3FFF: PPU registers (8, repeated)
    for (int i = 0; i < 0x2000; i++) {
        mm->addrs[0x2000 + i] = (MemoryAddress)
            {read_ppu_register, write_ppu_register, i % 8};
    }
    // 4000-4017: APU registers, not implemented for now,
    //            except overlapping non-APU functionality below:
    // 4014: OAM DMA register
    mm->addrs[0x4014].write_func = write_oam_dma;
    // 4016-4017: Controller I/O
    mm->addrs[0x4016] = (MemoryAddress)
        {read_controllers, write_controller_latch, 0};
    mm->addrs[0x4017] = (MemoryAddress)
        {read_controllers, NULL, 1};
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
    // 3F00-3FFF: Palettes
    for (int i = 0x3F00; i < 0x4000; i += 0x20) {
        for (int j = 0; j < 8; j++) {
            mm->addrs[i + (j * 0x04)] = (MemoryAddress)
                {read_background_colors, write_background_colors, j % 4};
        }
        for (int j = 0; j < 24; j++) {
            mm->addrs[i + (j / 3 * 4) + (j % 3) + 1] = (MemoryAddress)
                {read_palettes, write_palettes, j};
        }
    }
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
