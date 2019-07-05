#include <string.h>

#include "memory_maps.h"

#include "cartridge.h"
#include "machine.h"
#include "ppu.h"

static void init_common(MemoryMap *mm, Cartridge *cart) {
    mm->last_read = 0;
    mm->cart = cart;
    memset(mm->addrs, 0, sizeof(MemoryAddress) * 0x10000);
}

// CPU MEMORY MAP ACCESSES //

static uint8_t read_wram(MemoryMap *mm, int offset) {
    MemoryMapCPUInternal *internal = mm->internal;
    return internal->wram[offset];
}
static void write_wram(MemoryMap *mm, int offset, uint8_t value) {
    MemoryMapCPUInternal *internal = mm->internal;
    internal->wram[offset] = value;
}

static uint8_t read_ppu_register(MemoryMap *mm, int offset) {
    MemoryMapCPUInternal *internal = mm->internal;
    return ppu_read_register(internal->ppu, offset);
}
static void write_ppu_register(MemoryMap *mm, int offset, uint8_t value) {
    MemoryMapCPUInternal *internal = mm->internal;
    ppu_write_register(internal->ppu, offset, value);
}

static void write_oam_dma(MemoryMap *mm, int offset, uint8_t value) {
    if (value == 0x40) {
        return; // Avoid a (very unlikely) infinite loop
    }
    MemoryMapCPUInternal *internal = mm->internal;
    uint8_t page[0x100];
    uint16_t page_addr = (uint16_t)value << 8;
    for (int i = 0; i < 0x100; i++) {
        page[i] = mm_read(mm, page_addr + i);
    }
    ppu_write_oam_dma(internal->ppu, page);
}

static uint8_t read_controllers(MemoryMap *mm, int offset) {
    MemoryMapCPUInternal *internal = mm->internal;
    uint8_t value = mm->last_read & 0b11100000;
    if (internal->controller_bit < 8) {
        if (internal->controllers[offset] & (1 << internal->controller_bit++)) {
            value++;
        }
    }
    return value;
}

static void write_controller_latch(MemoryMap *mm, int offset, uint8_t value) {
    if (value & 1) {
        MemoryMapCPUInternal *internal = mm->internal;
        internal->controller_bit = 0;
    }
}

// PPU MEMORY MAP ACCESSES //

static uint8_t read_nametables(MemoryMap *mm, int offset) {
    MemoryMapPPUInternal *internal = mm->internal;
    return internal->nt_layout[offset / SIZE_NAMETABLE]
                              [offset % SIZE_NAMETABLE];
}
static void write_nametables(MemoryMap *mm, int offset, uint8_t value) {
    MemoryMapPPUInternal *internal = mm->internal;
    internal->nt_layout[offset / SIZE_NAMETABLE]
                       [offset % SIZE_NAMETABLE] = value;
}

static uint8_t read_background_colors(MemoryMap *mm, int offset) {
    MemoryMapPPUInternal *internal = mm->internal;
    return internal->background_colors[offset];
}
static void write_background_colors(MemoryMap *mm, int offset, uint8_t value) {
    MemoryMapPPUInternal *internal = mm->internal;
    internal->background_colors[offset] = value;
}

static uint8_t read_palettes(MemoryMap *mm, int offset) {
    MemoryMapPPUInternal *internal = mm->internal;
    return internal->palettes[offset];
}
static void write_palettes(MemoryMap *mm, int offset, uint8_t value) {
    MemoryMapPPUInternal *internal = mm->internal;
    internal->palettes[offset] = value;
}

// PUBLIC FUNCTIONS //

void memory_map_cpu_init(MemoryMap *mm, MemoryMapCPUInternal *internal,
                         Cartridge *cart, PPUState *ppu) {
    init_common(mm, cart);
    
    mm->internal = internal;
    internal->ppu = ppu;
    memset(internal->wram, 0, SIZE_WRAM);
    internal->controllers[0] = internal->controllers[1] = 0;
    internal->controller_bit = 8;
    
    // Populate the address map
    int i;
    // 0000-1FFF: WRAM (2kB, repeated)
    for (i = 0; i < SIZE_WRAM; i++) {
        mm->addrs[i] = (MemoryAddress) {read_wram, write_wram, i % SIZE_WRAM};
    }
    // 2000-3FFF: PPU registers (8, repeated)
    for (i = 0; i < 0x2000; i++) {
        mm->addrs[i + 0x2000] = (MemoryAddress)
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

void memory_map_ppu_init(MemoryMap *mm, MemoryMapPPUInternal *internal,
                         Cartridge *cart) {
    init_common(mm, cart);
    mm->internal = internal;
    
    // Wipe the various memory structures
    memset(internal->nametables[0], 0, SIZE_NAMETABLE);
    memset(internal->nametables[1], 0, SIZE_NAMETABLE);
    memset(internal->background_colors, 0, 4);
    memset(internal->palettes, 0, 8 * 3);
    
    // Define nametable memory layout
    internal->nt_layout[0] = internal->nametables[0];
    internal->nt_layout[1] = internal->nametables[cart->mirroring ? 1 : 0];
    internal->nt_layout[2] = internal->nametables[cart->mirroring ? 0 : 1];
    internal->nt_layout[3] = internal->nametables[1];
    
    // Populate the address map
    int i, j;
    // 0000-1FFF: Cartridge I/O, defined by the mapper's init
    // 2000-3EFF: Nametables
    for (i = 0; i < 0x1EFF; i++) {
        mm->addrs[i + 0x2000] = (MemoryAddress)
            {read_nametables, write_nametables, i % (SIZE_NAMETABLE * 4)};
    }
    // 3F00-3FFF: Palettes
    for (i = 0x3F00; i < 0x4000; i += 0x20) {
        for (j = 0; j < 8; j++) {
            mm->addrs[i + (j * 0x04)] = (MemoryAddress)
                {read_background_colors, write_background_colors, j % 4};
        }
        for (j = 0; j < 24; j++) {
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
