#ifndef f_ppu_h
#define f_ppu_h

#include "../common.h"

// Bit fields
#define CTRL_SCROLL_PAGE_X 1
#define CTRL_SCROLL_PAGE_Y (1 << 1)
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
// OAM_ATTR 0-1: Palette
// OAM_ATTR 2-4: Unused
#define OAM_ATTR_UNDER_BG (1 << 5)
#define OAM_ATTR_FLIP_H (1 << 6)
#define OAM_ATTR_FLIP_V (1 << 7)

// OAM property offsets
#define OAM_Y 0
#define OAM_PATTERN 1
#define OAM_ATTRS 2
#define OAM_X 3

// Registers
#define PPUCTRL 0
#define PPUMASK 1
#define PPUSTATUS 2
#define OAMADDR 3
#define OAMDATA 4
#define PPUSCROLL 5
#define PPUADDR 6
#define PPUDATA 7

// Screen dimensions
#define WIDTH 256
#define WIDTH_ADJUSTED (WIDTH * 8 / 7)
#define WIDTH_PP (WIDTH * 8)
#define HEIGHT 240
#define HEIGHT_CROPPED 224
#define HEIGHT_PP (HEIGHT_CROPPED * 7)

// Tasks array
#define TASK_SPRITE 0
#define TASK_FETCH 1
#define TASK_UPDATE 2

#define PPU_CYCLES_PER_SCANLINE 341

#define LIGHTGUN_COOLDOWN 26

// Forward declarations
typedef struct CPUState CPUState;
typedef struct PPUState PPUState;
typedef struct MemoryMap MemoryMap;

struct PPUState {
    CPUState *cpu;
    MemoryMap *mm;
    
    // Object Attribute Memory, ie. the sprites
    uint8_t oam[0x100];
    uint8_t oam_addr;
    uint8_t oam2[32];
    
    // Colors
    uint8_t background_colors[4];
    uint8_t palettes[8 * 3];

    // External registers
    uint8_t ctrl; // Write-only
    uint8_t mask; // Write-only
    uint8_t status; // Read-only
    
    // Internal registers
    uint16_t v;
    uint16_t t;
    uint8_t x;
    bool w;
    
    // Latches
    uint8_t reg_latch;
    uint8_t ppudata_latch;
    
    // Execution counters
    uint64_t time;
    int cycle;
    int scanline;
    int frame;
    
    // Rendering pipeline
    void (*tasks[PPU_CYCLES_PER_SCANLINE][4])(PPUState *);
    uint16_t f_nt, f_pt0, f_pt1;
    uint8_t f_at;
    uint16_t bg_pt0, bg_pt1;
    uint16_t bg_at0, bg_at1;
    uint8_t s_pt0[8], s_pt1[8];
    uint8_t s_attrs[8];
    uint8_t s_x[8];
    int s_total;
    bool s_has_zero, s_has_zero_next;
    
    // Raw screen data, in ARGB8888 format
    uint32_t screen[WIDTH * HEIGHT];
    
    // Lightgun sensor handling
    int lightgun_pos;
    int lightgun_sensor;
};

void ppu_init(PPUState *ppu, MemoryMap *mm, CPUState *cpu);
bool ppu_step(PPUState *ppu, bool verbose);

#endif /* f_ppu_h */
