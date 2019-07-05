#include <string.h>

#include "memory_maps.h"

#include "cartridge.h"
#include "machine.h"
#include "ppu.h"

static void init_common(MemoryMap *mm, Cartridge *cart) {
    mm->last_read = 0;
    mm->cart = cart;
    memset(mm->addrs, 0, sizeof(mm->addrs));
}

// CPU MEMORY MAP ACCESSES //

static uint8_t read_wram(MemoryMap *mm, int offset) {
    return mm->data.cpu.wram[offset];
}
static void write_wram(MemoryMap *mm, int offset, uint8_t value) {
    mm->data.cpu.wram[offset] = value;
}

static uint8_t read_ppu_register(MemoryMap *mm, int offset) {
    return ppu_read_register(mm->data.cpu.ppu, offset);
}
static void write_ppu_register(MemoryMap *mm, int offset, uint8_t value) {
    ppu_write_register(mm->data.cpu.ppu, offset, value);
}

static void write_oam_dma(MemoryMap *mm, int offset, uint8_t value) {
    if (value == 0x40) {
        return; // Avoid a (very unlikely) infinite loop
    }
    uint8_t page[0x100];
    uint16_t page_addr = (uint16_t)value << 8;
    for (int i = 0; i < 0x100; i++) {
        page[i] = mm_read(mm, page_addr + i);
    }
    ppu_write_oam_dma(mm->data.cpu.ppu, page);
}

static uint8_t read_controllers(MemoryMap *mm, int offset) {
    uint8_t value = mm->last_read & 0b11100000;
    if (mm->data.cpu.controller_bit[offset] < 8) {
        if (mm->data.cpu.controllers[offset] &
            (1 << mm->data.cpu.controller_bit[offset]++)) {
            value++;
        }
    }
    return value;
}

static void write_controller_latch(MemoryMap *mm, int offset, uint8_t value) {
    if (value & 1) {
        mm->data.cpu.controller_bit[0] = mm->data.cpu.controller_bit[1] = 0;
    }
}

// PPU MEMORY MAP ACCESSES //

static uint8_t read_nametables(MemoryMap *mm, int offset) {
    return mm->data.ppu.nt_layout[offset / SIZE_NAMETABLE]
                                 [offset % SIZE_NAMETABLE];
}
static void write_nametables(MemoryMap *mm, int offset, uint8_t value) {
    mm->data.ppu.nt_layout[offset / SIZE_NAMETABLE]
                          [offset % SIZE_NAMETABLE] = value;
}

static uint8_t read_background_colors(MemoryMap *mm, int offset) {
    return mm->data.ppu.background_colors[offset];
}
static void write_background_colors(MemoryMap *mm, int offset, uint8_t value) {
    mm->data.ppu.background_colors[offset] = value;
}

static uint8_t read_palettes(MemoryMap *mm, int offset) {
    return mm->data.ppu.palettes[offset];
}
static void write_palettes(MemoryMap *mm, int offset, uint8_t value) {
    mm->data.ppu.palettes[offset] = value;
}

// PUBLIC FUNCTIONS //

void memory_map_cpu_init(MemoryMap *mm, Cartridge *cart, PPUState *ppu) {
    init_common(mm, cart);
    
    MemoryMapCPUData *data = &mm->data.cpu;
    data->ppu = ppu;
    memset(data->wram, 0, sizeof(data->wram));
    data->controllers[0] = data->controllers[1] = 0;
    data->controller_bit[0] = data->controller_bit[1] = 8;
    
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

void memory_map_ppu_init(MemoryMap *mm, Cartridge *cart) {
    init_common(mm, cart);
    
    MemoryMapPPUData *data = &mm->data.ppu;
    mm_ppu_set_nt_mirroring(data, (cart->mirroring ? VERTICAL : HORIZONTAL));
    
    // Wipe the various memory structures
    memset(data->nametables[0], 0, SIZE_NAMETABLE);
    memset(data->nametables[1], 0, SIZE_NAMETABLE);
    memset(data->background_colors, 0, sizeof(data->background_colors));
    memset(data->palettes, 0, sizeof(data->background_colors));
    
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
    // 4000-FFFF: Invalid range
}

uint8_t mm_read(MemoryMap *mm, uint16_t addr) {
    MemoryAddress *f = &mm->addrs[addr];
    if (f->read_func) {
        mm->last_read = (*f->read_func)(mm, f->offset);
    }
    return mm->last_read;
}
uint16_t mm_read_word(MemoryMap *mm, uint16_t addr) {
    return (uint16_t)mm_read(mm, addr) + ((uint16_t)mm_read(mm, addr + 1) << 8);
}

void mm_write(MemoryMap *mm, uint16_t addr, uint8_t value) {
    MemoryAddress *f = &mm->addrs[addr];
    if (f->write_func) {
        (*f->write_func)(mm, f->offset, value);
    }
}
void mm_write_word(MemoryMap *mm, uint16_t addr, uint16_t value) {
    mm_write(mm, addr, value & 0xff);
    mm_write(mm, addr + 1, value >> 8);
}

void mm_ppu_set_nt_mirroring(MemoryMapPPUData *data, NametableMirroring m) {
    static const int layouts[] = {
        0, 0, 0, 0, // SINGLE_A
        1, 1, 1, 1, // SINGLE_B
        0, 0, 1, 1, // HORIZONTAL
        0, 1, 0, 1, // VERTICAL
    };
    const int *layout = layouts + m * 4;
    for (int i = 0; i < 4; i++) {
        data->nt_layout[i] = data->nametables[layout[i]];
    }
}
