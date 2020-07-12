#include "ppu.h"

#include "../cpu/65xx.h"
#include "machine.h"
#include "memory_maps.h"

// NTSC palette, generated from https://bisqwit.iki.fi/utils/nespalette.php
// (default settings, gamma 1.8)
static const uint32_t colors_ntsc[] = {
    0x525252, 0x011A51, 0x0F0F65, 0x230663,
    0x36034B, 0x400426, 0x3F0904, 0x321300,
    0x1F2000, 0x0B2A00, 0x002F00, 0x002E0A,
    0x00262D, 0x000000, 0x000000, 0x000000,
    
    0xA0A0A0, 0x1E4A9D, 0x3837BC, 0x5828B8,
    0x752194, 0x84235C, 0x822E24, 0x6F3F00,
    0x515200, 0x316300, 0x1A6B05, 0x0E692E,
    0x105C68, 0x000000, 0x000000, 0x000000,
    
    0xFEFFFF, 0x699EFC, 0x8987FF, 0xAE76FF,
    0xCE6DF1, 0xE070B2, 0xDE7C70, 0xC8913E,
    0xA6A725, 0x81BA28, 0x63C446, 0x54C17D,
    0x56B3C0, 0x3C3C3C, 0x000000, 0x000000,
    
    0xFEFFFF, 0xBED6FD, 0xCCCCFF, 0xDDC4FF,
    0xEAC0F9, 0xF2C1DF, 0xF1C7C2, 0xE8D0AA,
    0xD9DA9D, 0xC9E29E, 0xBCE6AE, 0xB4E5C7,
    0xB5DFE4, 0xA9A9A9, 0x000000, 0x000000,
};

// This is the palette from the PC10/Vs. RGB PPU, in ARGB8888 format
/*static const uint32_t colors_2C03[] = {
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
};*/

// Source: http://graphics.stanford.edu/~seander/bithacks.html#BitReverseTable
static const uint8_t bit_reverse[] =
{
#   define R2(n)     n,     n + 2*64,     n + 1*64,     n + 3*64
#   define R4(n) R2(n), R2(n + 2*16), R2(n + 1*16), R2(n + 3*16)
#   define R6(n) R4(n), R4(n + 2*4 ), R4(n + 1*4 ), R4(n + 3*4 )
    R6(0), R6(2), R6(1), R6(3)
};

static inline void increment_mm_addr(PPU *ppu) {
    ppu->v += (ppu->ctrl & CTRL_ADDR_INC_32 ? 32 : 1);
}

static inline bool is_rendering(PPU *ppu) {
    return ppu->mask & (MASK_RENDER_BACKGROUND | MASK_RENDER_SPRITES);
}

// CYCLE TASKS //

static void task_render_pixel(PPU *ppu, const RenderPos *pos) {
    int s_index = 0;
    uint8_t s_attrs = 0;
    bool s_is_zero = false;
    int bg_index = 0;

    if (ppu->mask & MASK_RENDER_SPRITES) {
        // Decrement all sprites,
        // while looking for a matching non-transparent pixel
        const int set_s_index = ppu->mask & MASK_NOCLIP_SPRITES ||
                                pos->cycle >= 8;
        for (int s = 0; s < 8; s++) {
            if (ppu->s_x[s]) {
                ppu->s_x[s]--;
            } else {
                if (set_s_index) {
                    s_index = ((ppu->s_pt0[s] & 128) >> 7) |
                              ((ppu->s_pt1[s] & 128) >> 6);
                    if (s_index) {
                        s_attrs = ppu->s_attrs[s];
                        s_is_zero = ppu->s_has_zero && !s;
                        ppu->s_pt0[s] <<= 1;
                        ppu->s_pt1[s] <<= 1;
                        s++;
                        for (; s < 8; s++) {
                            if (ppu->s_x[s]) {
                                ppu->s_x[s]--;
                            } else {
                                ppu->s_pt0[s] <<= 1;
                                ppu->s_pt1[s] <<= 1;
                            }
                        }
                        break;
                    }
                }
                ppu->s_pt0[s] <<= 1;
                ppu->s_pt1[s] <<= 1;
            }
        }
    }

    if (ppu->mask & (MASK_RENDER_BACKGROUND | MASK_NOCLIP_BACKGROUND) || pos->cycle >= 8) {
        bg_index = (((ppu->bg_pt0 << ppu->x) & 32768) >> 15) |
                   (((ppu->bg_pt1 << ppu->x) & 32768) >> 14);
    }

    if (bg_index && s_index && s_is_zero) {
        // TODO: delay by 1/2 (??) cycles
        ppu->status |= STATUS_SPRITE0_HIT;
    }

    int color;
    if (s_index && (!(s_attrs & OAM_ATTR_UNDER_BG) || !bg_index)) {
        color = ppu->palettes[((s_attrs & 0b11) + 4) * 3 + s_index - 1];
    } else if (bg_index) {
        int palette = (((ppu->bg_at0 << ppu->x) & 32768) >> 15) |
                      (((ppu->bg_at1 << ppu->x) & 32768) >> 14);
        color = ppu->palettes[palette * 3 + bg_index - 1];
    } else {
        color = ppu->background_colors[0];
    }

    const int pixel = pos->scanline * WIDTH + pos->cycle;
    ppu->screen[pixel] = colors_ntsc[color];
    if (pixel == *ppu->lightgun_pos && (color == 0x20 || color == 0x30)) {
        ppu->lightgun_sensor = LIGHTGUN_COOLDOWN;
    }

    ppu->bg_at0 <<= 1;
    ppu->bg_at1 <<= 1;
    ppu->bg_pt0 <<= 1;
    ppu->bg_pt1 <<= 1;
}

static void task_sprite_clear(PPU *ppu, const RenderPos *pos) {
    if (pos->scanline < 0) {
        return;
    }
    memset(ppu->oam2, 0xFF, sizeof(ppu->oam2));
    ppu->s_total = 0;
}

static void task_sprite_eval(PPU *ppu, const RenderPos *pos) {
    if (pos->scanline < 0) {
        return;
    }
    const uint8_t *spr = ppu->oam + ((pos->cycle - 65) / 3 * 4);
    const int sprite_height = (ppu->ctrl & CTRL_8x16_SPRITES ? 16 : 8);
    if (spr[OAM_Y] <= pos->scanline &&
        (spr[OAM_Y] + sprite_height - 1) >= pos->scanline) {
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

static void task_fetch_nt(PPU *ppu, const RenderPos *pos) {
    ppu->f_nt = mm_read(ppu->mm, 0x2000 | (ppu->v & 0x0FFF));
}

static void task_fetch_at(PPU *ppu, const RenderPos *pos) {
    ppu->f_at = mm_read(ppu->mm, 0x23C0 | (ppu->v & 0x0C00) |
                                          ((ppu->v >> 4) & 0x38) |
                                          ((ppu->v >> 2) & 0x07));
}

static uint8_t fetch_bg_pt(PPU *ppu, int offset) {
    uint16_t pt_addr = (ppu->f_nt << 4) | ((ppu->v & 0x7000) >> 12) | offset;
    if (ppu->ctrl & CTRL_PT_BACKGROUND) {
        pt_addr |= (1 << 12);
    }
    return mm_read(ppu->mm, pt_addr);
}

static void task_fetch_bg_pt0(PPU *ppu, const RenderPos *pos) {
    ppu->f_pt0 = fetch_bg_pt(ppu, 0);
}

static void task_fetch_bg_pt1(PPU *ppu, const RenderPos *pos) {
    ppu->f_pt1 = fetch_bg_pt(ppu, 8);

    // Fill the stacks
    if (pos->cycle > 256) {
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

static uint8_t fetch_spr_pt(PPU *ppu, int scanline, int i, int offset) {
    const bool sprite_16mode = (ppu->ctrl & CTRL_8x16_SPRITES);
    uint8_t *spr = ppu->oam2 + (i * 4);
    int row = scanline - spr[OAM_Y];
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
    if (i >= ppu->s_total) {
        p = 0;
    } else if (spr[OAM_ATTRS] & OAM_ATTR_FLIP_H) {
        p = bit_reverse[p];
    }
    return p;
}

static void task_fetch_spr_pt0(PPU *ppu, const RenderPos *pos) {
    int i = (pos->cycle - 261) / 8;
    ppu->s_pt0[i] = fetch_spr_pt(ppu, pos->scanline, i, 0);
    
    ppu->s_attrs[i] = ppu->oam2[i * 4 + OAM_ATTRS];
}

static void task_fetch_spr_pt1(PPU *ppu, const RenderPos *pos) {
    int i = (pos->cycle - 263) / 8;
    ppu->s_pt1[i] = fetch_spr_pt(ppu, pos->scanline, i, 8);
    
    ppu->s_x[i] = ppu->oam2[i * 4 + OAM_X];
    
    ppu->s_has_zero = ppu->s_has_zero_next;
}

static void task_update_inc_hori_v(PPU *ppu, const RenderPos *pos) {
    if ((ppu->v & 0b11111) == 0b11111) {
        ppu->v &= ~0b11111;
        ppu->v ^= (1 << 10);
    } else {
        ppu->v++;
    }
}

static void task_update_inc_vert_v(PPU *ppu, const RenderPos *pos) {
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

static void task_update_hori_v_hori_t(PPU *ppu,
                                      const RenderPos *pos) {
    ppu->v = (ppu->v & ~0x41F) | (ppu->t & 0x41F);
}

static void task_update_vert_v_vert_t(PPU *ppu,
                                      const RenderPos *pos) {
    if (pos->scanline == -1) {
        ppu->v = (ppu->v & ~0x7BE0) | (ppu->t & 0x7BE0);
    }
}

// MEMORY I/O //

static uint8_t read_register(Machine *vm, uint16_t addr) {
    PPU *ppu = &vm->ppu;
    switch (addr & 7) {
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

static void write_register(Machine *vm, uint16_t addr, uint8_t value) {
    PPU *ppu = &vm->ppu;
    ppu->reg_latch = value;
    uint8_t old_ctrl;
    uint16_t d;
    switch (addr & 7) {
        case PPUCTRL:
            old_ctrl = ppu->ctrl;
            ppu->ctrl = value;
            ppu->t = (ppu->t & ~(0b11 << 10)) |
                     (((uint16_t)value & 0b11) << 10);
            if (!(old_ctrl & CTRL_NMI_ON_VBLANK) &&
                value & CTRL_NMI_ON_VBLANK &&
                ppu->status & STATUS_VBLANK) {
                vm->cpu.nmi = true;
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

static void write_oam_dma(Machine *vm, uint16_t addr, uint8_t value) {
    if (value == 0x40) {
        return; // Avoid a (very unlikely) infinite loop
    }
    uint8_t page[0x100];
    uint16_t page_addr = (uint16_t)value << 8;
    for (int i = 0; i < 0x100; i++) {
        page[i] = mm_read(&vm->cpu_mm, page_addr + i);
    }
    memcpy(vm->ppu.oam, page, 0x100);
    machine_stall_cpu(vm, 0x200);
}

static uint8_t read_background_colors(Machine *vm, uint16_t addr) {
    return vm->ppu.background_colors[(addr >> 2) & 3];
}
static void write_background_colors(Machine *vm, uint16_t addr, uint8_t value) {
    vm->ppu.background_colors[(addr >> 2) & 3] = value & MASK_COLOR;
}

static uint8_t read_palettes(Machine *vm, uint16_t addr) {
    return vm->ppu.palettes[3 * ((addr >> 2) & 7) + (addr & 3) - 1];
}
static void write_palettes(Machine *vm, uint16_t addr, uint8_t value) {
    vm->ppu.palettes[3 * ((addr >> 2) & 7) + (addr & 3) - 1] = value &
                                                               MASK_COLOR;
}

// PUBLIC FUNCTIONS //

void ppu_init(PPU *ppu, MemoryMap *mm, CPU65xx *cpu, int *lightgun_pos) {
    memset(ppu, 0, sizeof(PPU));
    ppu->mm = mm;
    ppu->cpu = cpu;
    ppu->lightgun_pos = lightgun_pos;
    
    // Fill the tasks array
    // sprite
    ppu->tasks[1][TASK_SPRITE] = task_sprite_clear;
    for (int i = 0; i < 64; i++) {
        ppu->tasks[65 + i * 3][TASK_SPRITE] = task_sprite_eval;
    }
    // fetch
    for (int i = 1; i < PPU_CYCLES_PER_SCANLINE; i += 8) {
        ppu->tasks[i][TASK_FETCH] = task_fetch_nt;
        ppu->tasks[i + 2][TASK_FETCH] = task_fetch_at;
    }
    for (int i = 5; i < PPU_CYCLES_PER_SCANLINE; i += 8) {
        ppu->tasks[i][TASK_FETCH] = task_fetch_bg_pt0;
        ppu->tasks[i + 2][TASK_FETCH] = task_fetch_bg_pt1;
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
    
    // CPU 2000-3FFF: PPU registers (8, repeated)
    MemoryMap *cpu_mm = cpu->mm;
    for (int i = 0x2000; i < 0x4000; i++) {
        cpu_mm->read[i] = read_register;
        cpu_mm->write[i] = write_register;
    }
    // CPU 4014: OAM DMA register
    cpu_mm->write[0x4014] = write_oam_dma;
    
    // PPU 3F00-3FFF: Palettes
    for (int i = 0x3F00; i < 0x4000; i++) {
        mm->read[i] = read_palettes;
        mm->write[i] = write_palettes;
    }
    for (int i = 0x3F00; i < 0x4000; i += 4) {
        mm->read[i] = read_background_colors;
        mm->write[i] = write_background_colors;
    }
}

void ppu_step(PPU *ppu, const RenderPos *pos, bool verbose) {
    if (verbose && !pos->cycle) {
        printf("-- Scanline %d --\n", pos->scanline);
    }

    if (pos->scanline < HEIGHT) {
        if (pos->scanline >= 0 && pos->cycle < WIDTH) {
            task_render_pixel(ppu, pos);
        }
        // Execute all tasks for that cycle
        if (is_rendering(ppu)) {
            for (int i = 0; i < 3; i++) {
                if (ppu->tasks[pos->cycle][i]) {
                    (*ppu->tasks[pos->cycle][i])(ppu, pos);
                }
            }
        }
    }

    // Check for flag operations
    if (pos->cycle == 1) {
        switch (pos->scanline) {
            case -1:
                ppu->status &= ~(STATUS_VBLANK |
                                 STATUS_SPRITE0_HIT | STATUS_SPRITE_OVERFLOW);
                break;
            case 241:
                ppu->status |= STATUS_VBLANK;
                if (ppu->ctrl & CTRL_NMI_ON_VBLANK) {
                    ppu->cpu->nmi = true;
                }
                break;
        }
    } else if (!pos->cycle && (ppu->lightgun_sensor > 0)) {
        ppu->lightgun_sensor--;
    }
}
