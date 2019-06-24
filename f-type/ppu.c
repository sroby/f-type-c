#include <string.h>

#include "ppu.h"
#include "cpu.h"
#include "memory_maps.h"

// This is the palette from the PC10/Vs. RGB PPU, in ARGB8888 format
static const uint32_t palette[] = {
    0x606060, 0x002080, 0x0000C0, 0x6040C0,
    0x800060, 0xA00060, 0xA02000, 0x804000,
    0x604000, 0x204000, 0x006020, 0x008000,
    0x004040, 0x000000, 0x000000, 0x000000,
    
    0xA0A0A0, 0x0060C0, 0x0040E0, 0x8000E0,
    0xA000E0, 0xE00080, 0xE00000, 0xC06000,
    0x806000, 0x208000, 0x008000, 0x00A060,
    0x008080, 0x000000, 0x000000, 0x000000,
    
    0xE0E0E0, 0x60A0E0, 0x8080E0, 0xC060E0,
    0xE000E0, 0xE060E0, 0xE08000, 0xE0A000,
    0xC0C000, 0x60C000, 0x00E000, 0x40E0C0,
    0x00E0E0, 0x000000, 0x000000, 0x000000,
    
    0xE0E0E0, 0xA0C0E0, 0xC0A0E0, 0xE0A0E0,
    0xE080E0, 0xE0A0A0, 0xE0C080, 0xE0E040,
    0xE0E060, 0xA0E040, 0x80E060, 0x40E0C0,
    0x80C0E0, 0x000000, 0x000000, 0x000000,
};

static bool has_addr_latch(PPUState *ppu, uint8_t value) {
    if (ppu->addr_latch_is_set) {
        return true;
    }
    ppu->addr_latch = value;
    ppu->addr_latch_is_set = true;
    return false;
}

static inline void increment_mm_addr(PPUState *ppu) {
    ppu->mm_addr += (ppu->ctrl & CTRL_ADDR_INC_32 ? 32 : 1);
}

// PUBLIC FUNCTIONS //

void ppu_init(PPUState *ppu, MemoryMap *mm, CPUState *cpu) {
    ppu->cpu = cpu;
    
    ppu->mm = mm;
    ppu->mm_addr = 0;
    
    memset(ppu->oam, 0, 0x100);
    ppu->oam_addr = 0;
    
    ppu->ctrl = ppu->mask = ppu->status = 0;
    
    ppu->reg_latch = ppu->addr_latch = 0;
    ppu->addr_latch_is_set = false;
    
    ppu->scroll_x = ppu->scroll_y = 0;
    
    ppu->t = 0;
    ppu->scanline = 0;
}

bool ppu_scanline(PPUState *ppu) {
    MemoryMapPPUInternal *internal = ppu->mm->internal;
    
    if (ppu->scanline == 0) {
        ppu->status &= ~(STATUS_VBLANK | STATUS_SPRITE0_HIT |
                         STATUS_SPRITE_OVERFLOW);
    }
    if (ppu->scanline < 240) {
        // Very temporary sprite 0 hit check, just to get that out of the way
        if (ppu->oam[0] == ppu->scanline) {
            ppu->status |= STATUS_SPRITE0_HIT;
        }
        
        int line_offset = ppu->scanline * 256;
        for (int i = 0; i < 256; i++) {
            ppu->screen[line_offset + i] =
                palette[internal->background_colors[0]];
        }
    }
    if (ppu->scanline == 240) {
        ppu->status |= STATUS_VBLANK;
        if (ppu->ctrl & CTRL_NMI_ON_VBLANK) {
            cpu_nmi(ppu->cpu);
        }
    }
    
    ppu->scanline = (ppu->scanline + 1) % 262;
    ppu->t++;
    return !ppu->scanline;
}

uint8_t ppu_read_register(PPUState *ppu, int reg) {
    switch (reg) {
        case PPUSTATUS:
            ppu->reg_latch = (ppu->reg_latch & 0b11111) | ppu->status;
            ppu->status &= ~(STATUS_VBLANK); // VBlank is cleared at read
            ppu->addr_latch_is_set = false; // and so is the address latch
            break;
        case OAMDATA:
            ppu->reg_latch = ppu->oam[ppu->oam_addr];
            break;
        case PPUDATA:
            ppu->reg_latch = mm_read(ppu->mm, ppu->mm_addr);
            increment_mm_addr(ppu);
            break;
    }
    return ppu->reg_latch;
}

void ppu_write_register(PPUState *ppu, int reg, uint8_t value) {
    ppu->reg_latch = value;
    uint8_t old_ctrl;
    switch (reg) {
        case PPUCTRL:
            old_ctrl = ppu->ctrl;
            ppu->ctrl = value;
            if (!(old_ctrl & CTRL_NMI_ON_VBLANK) &&
                value & CTRL_NMI_ON_VBLANK &&
                ppu->status & STATUS_VBLANK) {
                cpu_nmi(ppu->cpu);
            }
            break;
        case PPUMASK:
            ppu->mask = value;
            break;
        case OAMADDR:
            ppu->oam_addr = value;
            break;
        case OAMDATA:
            ppu->oam[ppu->oam_addr++] = value;
            break;
        case PPUSCROLL:
            if (has_addr_latch(ppu, value)) {
                ppu->scroll_x = ppu->addr_latch;
                ppu->scroll_y = value;
                ppu->addr_latch_is_set = false;
            }
            break;
        case PPUADDR:
            if (has_addr_latch(ppu, value)) {
                ppu->mm_addr = ((uint16_t)ppu->addr_latch << 8) + value;
                ppu->addr_latch_is_set = false;
            }
            break;
        case PPUDATA:
            mm_write(ppu->mm, ppu->mm_addr, value);
            increment_mm_addr(ppu);
            break;
    }
}

void ppu_write_oam_dma(PPUState *ppu, uint8_t *page) {
    memcpy(ppu->oam, page, 0x100);
    cpu_external_t_increment(ppu->cpu, 0x201);
}
