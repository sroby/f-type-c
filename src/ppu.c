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

// Source: http://graphics.stanford.edu/~seander/bithacks.html#BitReverseTable
static const uint8_t bit_reverse[] =
{
#   define R2(n)     n,     n + 2*64,     n + 1*64,     n + 3*64
#   define R4(n) R2(n), R2(n + 2*16), R2(n + 1*16), R2(n + 3*16)
#   define R6(n) R4(n), R4(n + 2*4 ), R4(n + 1*4 ), R4(n + 3*4 )
    R6(0), R6(2), R6(1), R6(3)
};

static inline void increment_mm_addr(PPUState *ppu) {
    ppu->v += (ppu->ctrl & CTRL_ADDR_INC_32 ? 32 : 1);
}

static inline bool is_rendering(PPUState *ppu) {
    return ppu->mask & (MASK_RENDER_BACKGROUND | MASK_RENDER_SPRITES);
}

// CYCLE TASKS //

static void task_render_pixel(PPUState *ppu) {
    if (ppu->scanline < 0) {
        return;
    }
    
    int s_index = 0;
    uint8_t s_attrs = 0;
    bool s_is_zero = false;
    int bg_index = 0;
    
    if (ppu->mask & MASK_RENDER_SPRITES) {
        // Decrement all sprites,
        // while looking for a matching non-transparent pixel
        for (int s = 0; s < 8; s++) {
            if (ppu->s_x[s] > 0) {
                ppu->s_x[s]--;
            } else {
                if (!s_index && (ppu->mask & MASK_NOCLIP_SPRITES ||
                                 ppu->cycle >= 8)) {
                    s_index = ((ppu->s_pt0[s] & 128) >> 7) |
                              ((ppu->s_pt1[s] & 128) >> 6);
                    if (s_index) {
                        s_attrs = ppu->s_attrs[s];
                        s_is_zero = ppu->s_has_zero && !s;
                    }
                }
                ppu->s_pt0[s] <<= 1;
                ppu->s_pt1[s] <<= 1;
            }
        }
    }
    if (ppu->mask & MASK_RENDER_BACKGROUND) {
        if (ppu->mask & MASK_NOCLIP_BACKGROUND || ppu->cycle >= 8) {
            bg_index = (((ppu->bg_pt0 << ppu->x) & 32768) >> 15) |
                       (((ppu->bg_pt1 << ppu->x) & 32768) >> 14);
        }
    }
    
    if (bg_index && s_index && s_is_zero) {
        // TODO: delay by 1/2 (??) cycles
        ppu->status |= STATUS_SPRITE0_HIT;
    }

    int color;
    if (s_index && (!(s_attrs & OAM_ATTR_UNDER_BG) || !bg_index)) {
        color = ppu->mm->data.ppu.palettes[((s_attrs & 0b11) + 4) * 3 +
                                           s_index - 1];
    } else if (bg_index) {
        int palette = (((ppu->bg_at0 << ppu->x) & 32768) >> 15) |
                      (((ppu->bg_at1 << ppu->x) & 32768) >> 14);
        color = ppu->mm->data.ppu.palettes[palette * 3 + bg_index - 1];
    } else {
        color = ppu->mm->data.ppu.background_colors[0];
    }
    
    ppu->screen[ppu->scanline * WIDTH + ppu->cycle] = colors[color];

    ppu->bg_at0 <<= 1;
    ppu->bg_at1 <<= 1;
    ppu->bg_pt0 <<= 1;
    ppu->bg_pt1 <<= 1;
}

static void task_sprite_clear(PPUState *ppu) {
    if (ppu->scanline < 0) {
        return;
    }
    memset(ppu->oam2, 0xFF, sizeof(ppu->oam2));
    ppu->s_total = 0;
}

static void task_sprite_eval(PPUState *ppu) {
    if (ppu->scanline < 0) {
        return;
    }
    const uint8_t *spr = ppu->oam + ((ppu->cycle - 65) / 3 * 4);
    const int sprite_height = (ppu->ctrl & CTRL_8x16_SPRITES ? 16 : 8);
    if (spr[OAM_Y] <= ppu->scanline &&
        (spr[OAM_Y] + sprite_height - 1) >= ppu->scanline) {
        if (ppu->s_total >= 8) {
            // Not accurate behaviour, but very rarely used
            ppu->status |= STATUS_SPRITE_OVERFLOW;
        } else {
            memcpy(ppu->oam2 + (ppu->s_total * 4), spr, 4);
            ppu->s_total++;
        }
    }
    if (spr == ppu->oam) {
        ppu->s_has_zero_next = ppu->s_total;
    }
}

static void task_fetch_nt(PPUState *ppu) {
    ppu->f_nt = mm_read(ppu->mm, 0x2000 | (ppu->v & 0x0FFF));
}

static void task_fetch_at(PPUState *ppu) {
    ppu->f_at = mm_read(ppu->mm, 0x23C0 | (ppu->v & 0x0C00) |
                                          ((ppu->v >> 4) & 0x38) |
                                          ((ppu->v >> 2) & 0x07));
}

static uint8_t fetch_bg_pt(PPUState *ppu, int offset) {
    uint16_t pt_addr = (ppu->f_nt << 4) | ((ppu->v & 0x7000) >> 12) | offset;
    if (ppu->ctrl & CTRL_PT_BACKGROUND) {
        pt_addr |= (1 << 12);
    }
    return mm_read(ppu->mm, pt_addr);
}

static void task_fetch_bg_pt0(PPUState *ppu) {
    ppu->f_pt0 = fetch_bg_pt(ppu, 0);
}

static void task_fetch_bg_pt1(PPUState *ppu) {
    ppu->f_pt1 = fetch_bg_pt(ppu, 8);

    // Fill the stacks
    if (ppu->cycle > 256) {
        ppu->bg_pt0 <<= 8;
        ppu->bg_pt1 <<= 8;
        ppu->bg_at0 <<= 8;
        ppu->bg_at1 <<= 8;
    }
    
    ppu->bg_pt0 |= ppu->f_pt0;
    ppu->bg_pt1 |= ppu->f_pt1;
    
    int offset;
    if (((ppu->v >> 5) & 0b11) > 1) {
        offset = ((ppu->v & 0b11) > 1 ? 6 : 4);
    } else {
        offset = ((ppu->v & 0b11) > 1 ? 2 : 0);
    }
    int at = (ppu->f_at >> offset) & 0b11;
    if (at & 1) {
        ppu->bg_at0 |= 0xFF;
    }
    if (at & 2) {
        ppu->bg_at1 |= 0xFF;
    }
}

static uint8_t fetch_spr_pt(PPUState *ppu, int i, int offset) {
    if (i >= ppu->s_total) {
        return 0;
    }
    const bool sprite_16mode = (ppu->ctrl & CTRL_8x16_SPRITES);
    uint8_t *spr = ppu->oam2 + (i * 4);
    int row = ppu->scanline - spr[OAM_Y];
    if (spr[OAM_ATTRS] & OAM_ATTR_FLIP_V) {
        row = (sprite_16mode ? 16 : 8) - row - 1;
    }
    bool bank = ppu->ctrl & CTRL_PT_SPRITES;
    uint8_t pt = spr[OAM_PATTERN];
    if (sprite_16mode) {
        bank = pt & 1;
        if (row >= 8) {
            pt |= 1;
        } else {
            pt &= ~1;
        }
    }
    uint16_t pt_addr = (pt << 4) | (row % 8) | offset;
    if (bank) {
        pt_addr |= (1 << 12);
    }
    uint8_t p = mm_read(ppu->mm, pt_addr);
    if (spr[OAM_ATTRS] & OAM_ATTR_FLIP_H) {
        p = bit_reverse[p];
    }
    return p;
}

static void task_fetch_spr_pt0(PPUState *ppu) {
    int i = (ppu->cycle - 261) / 8;
    ppu->s_pt0[i] = fetch_spr_pt(ppu, i, 0);
    
    ppu->s_attrs[i] = ppu->oam2[i * 4 + OAM_ATTRS];
}

static void task_fetch_spr_pt1(PPUState *ppu) {
    int i = (ppu->cycle - 263) / 8;
    ppu->s_pt1[i] = fetch_spr_pt(ppu, i, 8);
    
    ppu->s_x[i] = ppu->oam2[i * 4 + OAM_X];
    
    ppu->s_has_zero = ppu->s_has_zero_next;
}

static void task_update_inc_hori_v(PPUState *ppu) {
    if ((ppu->v & 0b11111) == 0b11111) {
        ppu->v &= ~0b11111;
        ppu->v ^= (1 << 10);
    } else {
        ppu->v++;
    }
}

static void task_update_inc_vert_v(PPUState *ppu) {
    if ((ppu->v & 0x7000) == 0x7000) {
        ppu->v &= ~0x7000;
        uint16_t y = (ppu->v & 0x3E0) >> 5;
        if (y == 29) {
            y = 0;
            ppu->v ^= 0x800;
        } else if (y == 31) {
            y = 0;
        } else {
            y++;
        }
        ppu->v = (ppu->v & ~0x3E0) | (y << 5);
    } else {
        ppu->v += 0x1000;
    }
}

static void task_update_hori_v_hori_t(PPUState *ppu) {
    ppu->v = (ppu->v & ~0x41F) | (ppu->t & 0x41F);
}

static void task_update_vert_v_vert_t(PPUState *ppu) {
    if (ppu->scanline == -1) {
        ppu->v = (ppu->v & ~0x7BE0) | (ppu->t & 0x7BE0);
    }
}

// PUBLIC FUNCTIONS //

void ppu_init(PPUState *ppu, MemoryMap *mm, CPUState *cpu) {
    memset(ppu, 0, sizeof(PPUState));
    ppu->mm = mm;
    ppu->cpu = cpu;
    ppu->scanline = -1;
    
    // Fill the tasks array
    // sprite
    ppu->tasks[1][TASK_SPRITE] = task_sprite_clear;
    for (int i = 0; i < 64; i++) {
        ppu->tasks[65 + i * 3][TASK_SPRITE] = task_sprite_eval;
    }
    // fetch
    for (int i = 0; i < 336; i += 8) {
        ppu->tasks[i + 1][TASK_FETCH] = task_fetch_nt;
        ppu->tasks[i + 3][TASK_FETCH] = task_fetch_at;
        ppu->tasks[i + 5][TASK_FETCH] = task_fetch_bg_pt0;
        ppu->tasks[i + 7][TASK_FETCH] = task_fetch_bg_pt1;
    }
    for (int i = 0; i < 64; i += 8) {
        ppu->tasks[261 + i][TASK_FETCH] = task_fetch_spr_pt0;
        ppu->tasks[263 + i][TASK_FETCH] = task_fetch_spr_pt1;
    }
    // update
    for (int i = 8; i < 256; i += 8) {
        ppu->tasks[i][TASK_UPDATE] = task_update_inc_hori_v;
    }
    ppu->tasks[256][TASK_UPDATE] = task_update_inc_vert_v;
    ppu->tasks[257][TASK_UPDATE] = task_update_hori_v_hori_t;
    for (int i = 280; i < 305; i++) {
        ppu->tasks[i][TASK_UPDATE] = task_update_vert_v_vert_t;
    }
    ppu->tasks[328][TASK_UPDATE] = task_update_inc_hori_v;
    ppu->tasks[336][TASK_UPDATE] = task_update_inc_hori_v;
}

bool ppu_step(PPUState *ppu, bool verbose) {
    if (verbose && !ppu->cycle) {
        printf("-- Scanline %d --\n", ppu->scanline);
    }
    
    if (ppu->scanline >= 0 && ppu->scanline < HEIGHT &&
        ppu->cycle < WIDTH) {
        task_render_pixel(ppu);
    }
    
    // Execute all tasks for that cycle
    if (ppu->scanline < 240 && is_rendering(ppu)) {
        for (int i = 0; i < 3; i++) {
            if (ppu->tasks[ppu->cycle][i]) {
                (*ppu->tasks[ppu->cycle][i])(ppu);
            }
        }
    }
    
    // Check for flag operations
    if (ppu->cycle == 1) {
        switch (ppu->scanline) {
            case -1:
                ppu->status &= ~(STATUS_VBLANK |
                                 STATUS_SPRITE0_HIT | STATUS_SPRITE_OVERFLOW);
                break;
            case 241:
                ppu->status |= STATUS_VBLANK;
                if (ppu->ctrl & CTRL_NMI_ON_VBLANK) {
                    cpu_nmi(ppu->cpu);
                }
                break;
        }
    }
    
    // Increment the counters
    ppu->time++;
    ppu->cycle = (ppu->cycle + 1) % PPU_CYCLES_PER_SCANLINE;
    if (ppu->scanline == -1 && ppu->cycle == PPU_CYCLES_PER_SCANLINE - 1 &&
        ppu->frame % 2) {
        // Skip last cycle of the pre-render line on odd frames
        ppu->cycle = 0;
    }
    if (ppu->cycle == 0) {
        ppu->scanline++;
        if (ppu->scanline == 261) {
            ppu->scanline = -1;
            ppu->frame++;
            return true;
        }
    }
    return false;
}

uint8_t ppu_read_register(PPUState *ppu, int reg) {
    switch (reg) {
        case PPUSTATUS:
            ppu->reg_latch = (ppu->reg_latch & 0b11111) | ppu->status;
            ppu->status &= ~(STATUS_VBLANK); // VBlank is cleared at read
            ppu->w = false; // and so is the address latch
            break;
        case OAMDATA:
            ppu->reg_latch = ppu->oam[ppu->oam_addr];
            break;
        case PPUDATA:
            if (ppu->v >= 0x3F00) {
                ppu->reg_latch = mm_read(ppu->mm, ppu->v);
            } else {
                ppu->reg_latch = ppu->ppudata_latch;
                ppu->ppudata_latch = mm_read(ppu->mm, ppu->v);
                increment_mm_addr(ppu);
            }
            break;
    }
    return ppu->reg_latch;
}

void ppu_write_register(PPUState *ppu, int reg, uint8_t value) {
    ppu->reg_latch = value;
    uint8_t old_ctrl;
    uint16_t d;
    switch (reg) {
        case PPUCTRL:
            old_ctrl = ppu->ctrl;
            ppu->ctrl = value;
            ppu->t = (ppu->t & ~(0b11 << 10)) |
                     (((uint16_t)value & 0b11) << 10);
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
            d = value;
            if (!ppu->w) {
                ppu->t = (ppu->t & ~0b11111) | (d >> 3);
                ppu->x = value & 0b111;
            } else {
                ppu->t = (ppu->t & 0b110000011111) | ((d & 0b111) << 12) |
                         ((d & 0b11111000) << 2);
            }
            ppu->w = !ppu->w;
            break;
        case PPUADDR:
            d = value;
            if (!ppu->w) {
                ppu->t = (ppu->t & 255) | ((d & 0b111111) << 8);
            } else {
                ppu->v = ppu->t = (ppu->t & ~255) | d;
            }
            ppu->w = !ppu->w;
            break;
        case PPUDATA:
            mm_write(ppu->mm, ppu->v, value);
            increment_mm_addr(ppu);
            break;
    }
}

void ppu_write_oam_dma(PPUState *ppu, uint8_t *page) {
    memcpy(ppu->oam, page, 0x100);
    cpu_external_t_increment(ppu->cpu, 0x201);
}
