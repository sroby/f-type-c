#include "ppu.h"

#include "cpu.h"
#include "memory_maps.h"

// This is the palette from the PC10/Vs. RGB PPU, in ARGB8888 format
static const uint32_t colors[] = {
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

static bool render_pattern_row(PPUState *ppu, uint16_t pt_addr, int pos_x,
                        int palette, bool flip_h, bool clip) {
    uint8_t *palettes = ppu->mm->data.ppu.palettes;
    const int line = ppu->scanline - 21;
    uint8_t b0 = mm_read(ppu->mm, pt_addr);
    uint8_t b1 = mm_read(ppu->mm, pt_addr | (1 << 3));
    if (!b0 && !b1) {
        return false;
    }
    for (int j = 0; j < 8; j++) {
        int pos = pos_x + j;
        if (pos < 0 || (clip && pos < 8)) {
            continue;
        }
        if (pos > 255) {
            break;
        }
        int jj = (flip_h ? j : 7 - j);
        int palette_index = (b0 & (1 << jj) ? 1 : 0) +
                            (b1 & (1 << jj) ? 2 : 0);
        if (palette_index) {
            ppu->screen[line * WIDTH + pos] =
                colors[palettes[palette * 3 + palette_index - 1]];
        }
    }
    return true;
}

static void render_sprites(PPUState *ppu, int n_sprites,
                           const uint8_t **sprites, bool front) {
    if (!n_sprites) {
        return;
    }
    const bool clip = !(ppu->mask & MASK_NOCLIP_SPRITES);
    const int line = ppu->scanline - 21;
    const bool tall = ppu->ctrl & CTRL_8x16_SPRITES;
    const int height = (tall ? 16 : 8);
    bool bank = ppu->ctrl & CTRL_PT_SPRITES;
    for (int i = n_sprites - 1; i >= 0; i--) {
        const uint8_t *sprite = sprites[i];
        if ((bool)(sprite[OAM_ATTRS] & OAM_ATTR_UNDER_BG) == front) {
            continue;
        }
        uint16_t row = line - sprite[OAM_Y] - 1;
        if (sprite[OAM_ATTRS] & OAM_ATTR_FLIP_V) {
            row = height - row - 1;
        }
        uint8_t pt = sprite[OAM_PATTERN];
        if (tall) {
            bank = pt & 1;
            if (row >= 8) {
                pt |= 1;
            } else {
                pt &= ~1;
            }
        }
        uint16_t pt_addr = ((uint16_t)pt << 4) | (row % 8);
        if (bank) {
            pt_addr |= (1 << 12);
        }
        int palette = (sprite[OAM_ATTRS] & 0b11) + 4;
        bool flip_h = sprite[OAM_ATTRS] & OAM_ATTR_FLIP_H;
        if (render_pattern_row(ppu, pt_addr, sprite[OAM_X], palette, flip_h,
                               clip) &&
            sprite == ppu->oam) {
            ppu->status |= STATUS_SPRITE0_HIT;
        }
    }
}

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
    
    memset(ppu->oam, 0, sizeof(ppu->oam));
    ppu->oam_addr = 0;
    
    ppu->ctrl = ppu->mask = ppu->status = 0;
    
    ppu->reg_latch = ppu->ppudata_latch = ppu->addr_latch = 0;
    ppu->addr_latch_is_set = false;
    
    ppu->scroll_x = ppu->scroll_y = 0;
    
    ppu->t = 0;
    ppu->scanline = 0;
}

bool ppu_scanline(PPUState *ppu) {
    // Scanlines 0-19: Vblank period
    if (ppu->scanline == 0) {
        ppu->status |= STATUS_VBLANK;
        if (ppu->ctrl & CTRL_NMI_ON_VBLANK) {
            cpu_nmi(ppu->cpu);
        }
    }
    
    // Scanline 20: Pre-rendering
    else if (ppu->scanline == 20) {
        ppu->status &= ~(STATUS_VBLANK |
                         STATUS_SPRITE0_HIT |
                         STATUS_SPRITE_OVERFLOW);
    }
    
    // Scanlines 21-260: Rendering (240 lines)
    else if (ppu->scanline >= 21 && ppu->scanline <= 260) {
        const int line = ppu->scanline - 21;
        
        // Fill the line with the current background colour
        uint32_t bg_color = colors[ppu->mm->data.ppu.background_colors[0]];
        for (int i = 0; i < WIDTH; i++) {
            ppu->screen[line * WIDTH + i] = bg_color;
        }
        
        // Fetch up to 8 suitable sprites
        const uint8_t *sprites[8];
        const int sprite_height = (ppu->ctrl & CTRL_8x16_SPRITES ? 16 : 8);
        int n_sprites = 0;
        if (line && ppu->mask & MASK_RENDER_SPRITES) {
            for (int i = 0; i < 64; i++) {
                int pos_y = ppu->oam[i * 4] + 1;
                if (pos_y <= line && (pos_y + sprite_height - 1) >= line) {
                    if (n_sprites >= 8) {
                        ppu->status |= STATUS_SPRITE_OVERFLOW;
                        break;
                    }
                    sprites[n_sprites++] = ppu->oam + (i * 4);
                }
            }
        }
        render_sprites(ppu, n_sprites, sprites, false);
        
        // Render background
        if (ppu->mask & MASK_RENDER_BACKGROUND) {
            bool clip = !(ppu->mask & MASK_NOCLIP_BACKGROUND);
            uint16_t nt_page = ppu->ctrl & 0b11;
            int nt_x = ppu->scroll_x / 8;
            int screen_x = -(ppu->scroll_x % 8);
            uint16_t row = (ppu->scroll_y + line) % 8;
            int nt_y = (ppu->scroll_y + line) / 8;
            if (nt_y >= HEIGHT_NT) {
                nt_y -= HEIGHT_NT;
                nt_page ^= 2;
            }
            int at = 0x100;
            while (screen_x < WIDTH) {
                if (nt_x >= WIDTH_NT) {
                    nt_x -= WIDTH_NT;
                    nt_page ^= 1;
                }
                uint16_t nt_page_addr = 0x2000 + nt_page * 0x400;
                uint16_t nt_addr = nt_page_addr + nt_x + nt_y * WIDTH_NT;
                uint16_t pt_addr = ((uint16_t)mm_read(ppu->mm, nt_addr) << 4) |
                                   row;
                if (ppu->ctrl & CTRL_PT_BACKGROUND) {
                    pt_addr |= (1 << 12);
                }

                // Find the palette in the attribute table
                if (at == 0x100 || !(nt_x % 4)) {
                    at = mm_read(ppu->mm,
                            nt_page_addr + 0x3C0 + nt_x / 4 + nt_y / 4 * 8);
                }
                int palette;
                // There's probably a better way to do this
                if (nt_x % 4 < 2) {
                    if (nt_y % 4 < 2) {
                        palette = at & 0b11;
                    } else {
                        palette = (at & 0b110000) >> 4;
                    }
                } else {
                    if (nt_y % 4 < 2) {
                        palette = (at & 0b1100) >> 2;
                    } else {
                        palette = at >> 6;
                    }
                }
                
                render_pattern_row(ppu, pt_addr, screen_x, palette, false,
                                   clip);
                nt_x++;
                screen_x += 8;
            }
        }
        
        render_sprites(ppu, n_sprites, sprites, true);
        
        if (ppu->scanline_callback) {
            (*ppu->scanline_callback)(ppu);
        }
    }
    
    // Scanline 261: Post-rendering
    // (this is a wasted line that does nothing)
    
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
            ppu->reg_latch = ppu->ppudata_latch;
            ppu->ppudata_latch = mm_read(ppu->mm, ppu->mm_addr);
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
