#ifndef ppu_h
#define ppu_h

#include "common.h"

// Bit fields
// CTRL 0-1: Base nametable address
#define CTRL_ADDR_INC_32 (1 << 2)
#define CTRL_PT_SPRITES (1 << 3)
#define CTRL_PT_BACKGROUND (1 << 4)
#define CTRL_8x16_SPRITES (1 << 5)
#define CTRL_PPU_SELECT (1 << 6)
#define CTRL_NMI_ON_VBLANK (1 << 7)
#define MASK_GREYSCALE 1
#define MASK_NOCLIP_BACKGROUND (1 << 1)
#define MASK_NOCLIP_SPRITES (1 << 2)
#define MASK_RENDER_BACKGROUND (1 << 3)
#define MASK_RENDER_SPRITES (1 << 4)
#define MASK_EMPHASIS_RED (1 << 5)
#define MASK_EMPHASIS_GREEN (1 << 6)
#define MASK_EMPHASIS_BLUE (1 << 7)
// STATUS 0-4: Unused
#define STATUS_SPRITE_OVERFLOW (1 << 5)
#define STATUS_SPRITE0_HIT (1 << 6)
#define STATUS_VBLANK (1 << 7)

// Registers
#define PPUCTRL 0
#define PPUMASK 1
#define PPUSTATUS 2
#define OAMADDR 3
#define OAMDATA 4
#define PPUSCROLL 5
#define PPUADDR 6
#define PPUDATA 7

// Forward declarations
typedef struct CPUState CPUState;
typedef struct PPUState PPUState;
typedef struct MemoryMap MemoryMap;

struct PPUState {
    CPUState *cpu;
    
    MemoryMap *mm;
    uint16_t mm_addr;
    
    // Object Attribute Memory, ie. the sprites
    uint8_t oam[0x100];
    uint8_t oam_addr;

    // Registers
    uint8_t ctrl; // Write-only
    uint8_t mask; // Write-only
    uint8_t status; // Read-only

    // Latches
    uint8_t reg_latch;
    uint8_t addr_latch;
    bool addr_latch_is_set;
    
    uint8_t scroll_x, scroll_y;
    
    uint64_t t;
    int scanline;
};

void ppu_init(PPUState *ppu, MemoryMap *mm, CPUState *cpu);

bool ppu_scanline(PPUState *ppu);

uint8_t ppu_read_register(PPUState *ppu, int reg);
void ppu_write_register(PPUState *ppu, int reg, uint8_t value);
void ppu_write_oam_dma(PPUState *ppu, uint8_t *page);

#endif /* ppu_h */
