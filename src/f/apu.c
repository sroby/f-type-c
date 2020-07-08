#include "apu.h"

#include "../cpu/65xx.h"
#include "machine.h"
#include "memory_maps.h"

#define B_STR(x) (x ? "on" : "off")

const int dmc_rates[] = {428, 380, 340, 320, 286, 254, 226, 214,
                         190, 160, 142, 128, 106,  84,  72,  54};

const char pulse_sequences[][8] = {
    {1, 0, 1, 1, 1, 1, 1, 1},
    {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0},
    {1, 0, 0, 1, 1, 1, 1, 1},
};

const char triangle_sequence[] = {
    45, 42, 39, 36, 33, 30, 27, 24, 21, 18, 15, 12,  9,  6,  3,  0,
     0,  3,  6,  9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45,
};

const uint8_t counter_lengths[] = { 10, 254, 20,  2, 40,  4, 80,  6,
                                   160,   8, 60, 10, 14, 12, 26, 14,
                                    12,  16, 24, 18, 48, 20, 96, 22,
                                   192,  24, 72, 26, 16, 28, 32, 30};

const uint16_t noise_periods[] = {  4,   8,  16,  32,  64,   96,  128,  160,
                                  202, 254, 380, 508, 762, 1016, 2034, 4068};

const int16_t pulse_mix[] = {   0,  380,  751, 1114, 1468, 1813, 2151, 2481,
                             2804, 3120, 3429, 3731, 4026, 4315, 4599, 4876,
                             5148, 5414, 5674, 5930, 6180, 6426, 6667, 6903,
                             7135, 7362, 7585, 7804, 8019, 8230, 8438};

const int16_t tnd_mix[] = {
        0,   219,   437,   653,   867,  1080,  1290,  1499,
     1707,  1913,  2117,  2319,  2520,  2720,  2918,  3114,
     3309,  3502,  3694,  3884,  4073,  4261,  4447,  4632,
     4815,  4997,  5178,  5357,  5535,  5711,  5887,  6061,
     6234,  6405,  6576,  6745,  6912,  7079,  7245,  7409,
     7572,  7734,  7895,  8055,  8213,  8371,  8527,  8683,
     8837,  8990,  9143,  9294,  9444,  9593,  9741,  9888,
    10034, 10180, 10324, 10467, 10609, 10751, 10891, 11031,
    11169, 11307, 11444, 11580, 11715, 11849, 11983, 12115,
    12247, 12378, 12508, 12637, 12765, 12893, 13020, 13146,
    13271, 13395, 13519, 13642, 13764, 13886, 14006, 14126,
    14246, 14364, 14482, 14599, 14715, 14831, 14946, 15060,
    15174, 15287, 15400, 15511, 15622, 15733, 15842, 15952,
    16060, 16168, 16275, 16382, 16488, 16593, 16698, 16802,
    16906, 17009, 17112, 17214, 17315, 17416, 17516, 17616,
    17715, 17813, 17911, 18009, 18106, 18202, 18298, 18394,
    18489, 18583, 18677, 18770, 18863, 18956, 19047, 19139,
    19230, 19320, 19410, 19500, 19589, 19677, 19766, 19853,
    19940, 20027, 20114, 20199, 20285, 20370, 20454, 20539,
    20622, 20706, 20788, 20871, 20953, 21035, 21116, 21197,
    21277, 21357, 21437, 21516, 21595, 21673, 21751, 21829,
    21906, 21983, 22060, 22136, 22212, 22287, 22363, 22437,
    22512, 22586, 22659, 22733, 22806, 22878, 22951, 23023,
    23094, 23166, 23237, 23307, 23378, 23447, 23517, 23586,
    23656, 23724, 23793, 23861, 23928, 23996, 24063, 24130,
    24196, 24263, 24329,
};

const uint16_t sequence_lengths[] = {8, 8, 32};

// MEMORY I/O //

static void write_envelope_volume(Machine *vm, uint16_t addr, uint8_t value) {
    // Pulse, Noise
    WaveformChannel *ch = vm->apu.channels + ((addr >> 2) & 7);
    ch->duty = value >> 6;
    BIT_AS(ch->flags, CHF_HALT, BIT_CHECK(value, 5));
    BIT_AS(ch->flags, CHF_ENV_DISABLE, BIT_CHECK(value, 4));
    ch->volume = value & 0xF;
}

static void write_pulse_sweep(Machine *vm, uint16_t addr, uint8_t value) {
    WaveformChannel *ch = vm->apu.channels + ((addr >> 2) & 7);
    BIT_AS(ch->flags, CHF_SWEEP_ENABLE, BIT_CHECK(value, 7));
    ch->sweep_counter_load = (value >> 4) & 7;
    BIT_AS(ch->flags, CHF_SWEEP_NEGATE, BIT_CHECK(value, 3));
    ch->sweep_shift = value & 7;
    BIT_SET(ch->flags, CHF_SWEEP_RELOAD);
}

static void write_timer_low(Machine *vm, uint16_t addr, uint8_t value) {
    // Pulse, Triangle
    WaveformChannel *ch = vm->apu.channels + ((addr >> 2) & 7);
    ch->timer_load = (ch->timer_load & 0xFF00) | value;
}

static void write_length_counter_timer_high(Machine *vm, uint16_t addr,
                                            uint8_t value) {
    // Pulse, Triangle, Noise
    ChannelIndex n = ((addr >> 2) & 7);
    WaveformChannel *ch = vm->apu.channels + n;
    if (BIT_CHECK(vm->apu.ch_enabled, n)) {
        ch->length_counter = counter_lengths[value >> 3];
    }
    if (n != CH_NOISE) {
        ch->timer_load = (ch->timer_load & 0xFF) | ((value & 0b111) << 8);
    }
    if (n <= CH_PULSE_2) {
        ch->sequence = 0;
    }
    if (n == CH_TRIANGLE) {
        BIT_SET(vm->apu.flags, AF_LINEAR_COUNTER_RELOAD);
    }
    BIT_SET(ch->flags, CHF_ENV_START);
}

static void write_triangle_linear_counter(Machine *vm, uint16_t addr,
                                          uint8_t value) {
    APU *apu = &vm->apu;
    BIT_AS(apu->channels[CH_TRIANGLE].flags, CHF_HALT, BIT_CHECK(value, 7));
    apu->linear_counter_load = value & 0x7F;
}

static void write_noise_mode_period(Machine *vm, uint16_t addr, uint8_t value) {
    WaveformChannel *ch = vm->apu.channels + CH_NOISE;
    BIT_AS(ch->flags, CHF_NOISE_MODE, BIT_CHECK(value, 7));
    ch->timer_load = noise_periods[value & 0xF] / 2; // TODO do we need the /2?
}

static void write_dmc_flags_rate(Machine *vm, uint16_t addr, uint8_t value) {
    APU *apu = &vm->apu;
    
    bool irq_set = BIT_CHECK(value, 7);
    BIT_AS(apu->flags, AF_DMC_IRQ_ENABLE, irq_set);
    BIT_CLEAR_IF(vm->cpu.irq, IRQ_APU_DMC, !irq_set);
    
    BIT_AS(apu->flags, AF_DMC_LOOP, BIT_CHECK(value, 6));
    
    apu->dmc_timer_load = dmc_rates[value & 0xF] / 2;
}

static void write_dmc_load(Machine *vm, uint16_t addr, uint8_t value) {
    vm->apu.dmc_delta = value & 0x7F;
}

static void write_dmc_addr(Machine *vm, uint16_t addr, uint8_t value) {
    vm->apu.dmc_addr_load = 0xC000 + (value << 6);
}

static void write_dmc_length(Machine *vm, uint16_t addr, uint8_t value) {
    vm->apu.dmc_length = (value << 4) + 1;
}

static uint8_t read_status(Machine *vm, uint16_t addr) {
    APU *apu = &vm->apu;
    CPU65xx *cpu = &vm->cpu;
    uint8_t status = 0;
    for (int i = 0; i < 4; i++) {
        BIT_SET_IF(status, i, apu->channels[i].length_counter > 0);
    }
    BIT_SET_IF(status, CH_DMC, apu->dmc_remain > 0);
    BIT_SET_IF(status, 6, BIT_CHECK(cpu->irq, IRQ_APU_FRAME));
    BIT_SET_IF(status, 7, BIT_CHECK(cpu->irq, IRQ_APU_DMC));
    BIT_CLEAR(cpu->irq, IRQ_APU_FRAME);
    return status;
}

static void write_control(Machine *vm, uint16_t addr, uint8_t value) {
    APU *apu = &vm->apu;
    vm->apu.ch_enabled = value & 0b11111;
    for (int i = 0; i < 4; i++) {
        if (!BIT_CHECK(value, i)) {
            apu->channels[i].length_counter = 0;
        }
    }
    if (BIT_CHECK(value, CH_DMC)) {
        if (!apu->dmc_remain) {
            apu->dmc_remain = apu->dmc_length;
            apu->dmc_addr = apu->dmc_addr_load;
        }
    } else {
        apu->dmc_remain = 0;
    }
    BIT_CLEAR(vm->cpu.irq, IRQ_APU_DMC);
}

static void write_frame_counter(Machine *vm, uint16_t addr, uint8_t value) {
    APU *apu = &vm->apu;
    bool irq_set = BIT_CHECK(value, 6);
    BIT_AS(apu->flags, AF_FC_IRQ_DISABLE, irq_set);
    BIT_CLEAR_IF(vm->cpu.irq, IRQ_APU_FRAME, irq_set);
    
    BIT_AS(apu->flags, AF_FC_DIVIDER, BIT_CHECK(value, 7));
    apu->fc_timer = 0;
}

// FRAME COUNTER //

static void fc_quarter(APU *apu) {
    // Pulse and Noise envelopes
    const int ch_env[] = {CH_PULSE_1, CH_PULSE_2, CH_NOISE};
    for (int i = 0; i < (sizeof(ch_env) / sizeof(int)); i++) {
        WaveformChannel *ch = apu->channels + ch_env[i];
        if (BIT_CHECK(ch->flags, CHF_ENV_START)) {
            BIT_CLEAR(ch->flags, CHF_ENV_START);
            ch->env_divider = ch->volume;
            ch->env_decay = 15;
        } else if (ch->env_divider) {
            --ch->env_divider;
        } else {
            ch->env_divider = ch->volume;
            if (ch->env_decay) {
                --ch->env_decay;
            } else if (BIT_CHECK(ch->flags, CHF_HALT)) {
                ch->env_decay = 15;
            }
        }
    }
    
    // Triangle linear counter
    if (BIT_CHECK(apu->flags, AF_LINEAR_COUNTER_RELOAD)) {
        apu->linear_counter = apu->linear_counter_load;
    } else if (apu->linear_counter) {
        --apu->linear_counter;
    }
    BIT_CLEAR_IF(apu->flags, AF_LINEAR_COUNTER_RELOAD,
                 !BIT_CHECK(apu->channels[CH_TRIANGLE].flags, CHF_HALT));
}

static void fc_half(APU *apu) {
    // Length counters
    for (int i = 0; i < 4; i++) {
        WaveformChannel *ch = apu->channels + i;
        if (ch->length_counter && !BIT_CHECK(ch->flags, CHF_HALT)) {
            --ch->length_counter;
        }
    }
    
    // Pulse sweeps
    for (int i = 0; i < 2; i++) {
        WaveformChannel *ch = apu->channels + i;
        if (!ch->sweep_counter && BIT_CHECK(ch->flags, CHF_SWEEP_ENABLE) &&
            (ch->timer_load >= 8) && (ch->timer_load <= 0x7FF)) {
            uint16_t amount = ch->timer_load >> ch->sweep_shift;
            if (BIT_CHECK(ch->flags, CHF_SWEEP_NEGATE)) {
                amount += i - 1;
                if (ch->timer_load <= amount) {
                    ch->timer_load = 0;
                } else {
                    ch->timer_load -= amount;
                }
            } else {
                ch->timer_load += amount;
            }
        }
        if (!ch->sweep_counter || BIT_CHECK(ch->flags, CHF_SWEEP_RELOAD)) {
            ch->sweep_counter = ch->sweep_counter_load;
            BIT_CLEAR(ch->flags, CHF_SWEEP_RELOAD);
        } else {
            --ch->sweep_counter;
        }
    }
}

// PUBLIC FUNCTIONS //

void apu_init(APU *apu, CPU65xx *cpu, int16_t *audio_buffer, int *audio_pos) {
    memset(apu, 0, sizeof(APU));
    apu->cpu = cpu;
    apu->audio_buffer = audio_buffer;
    apu->audio_pos = audio_pos;
    
    apu->channels[CH_NOISE].sequence = 1;
    
    MemoryMap *mm = cpu->mm;
    
    // 4000-4007: Pulse channels
    for (int i = 0; i < 8; i += 4) {
        mm->write[0x4000 + i] = write_envelope_volume;
        mm->write[0x4001 + i] = write_pulse_sweep;
        mm->write[0x4002 + i] = write_timer_low;
        mm->write[0x4003 + i] = write_length_counter_timer_high;
    }
    // 4008-400B: Triangle channel
    mm->write[0x4008] = write_triangle_linear_counter;
    //        0x4009 Unused
    mm->write[0x400A] = write_timer_low;
    mm->write[0x400B] = write_length_counter_timer_high;
    // 400C-400F: Noise channel
    mm->write[0x400C] = write_envelope_volume;
    //        0x400D Unused
    mm->write[0x400E] = write_noise_mode_period;
    mm->write[0x400F] = write_length_counter_timer_high;
    // 4010-4013: DMC channel
    mm->write[0x4010] = write_dmc_flags_rate;
    mm->write[0x4011] = write_dmc_load;
    mm->write[0x4012] = write_dmc_addr;
    mm->write[0x4013] = write_dmc_length;
    // 4015: Status and control
    mm->read[0x4015] = read_status;
    mm->write[0x4015] = write_control;
    // 4017: Frame control (write only, overlaps controller #2 on read)
    mm->write[0x4017] = write_frame_counter;
}

void apu_step(APU *apu) {
    // Advance frame counter
    ++apu->fc_timer;
    if (!(apu->fc_timer % FC_CYCLES)) {
        switch (apu->fc_timer) {
            // 1/4 and 3/4
            case FC_CYCLES:
            case FC_CYCLES * 3:
                fc_quarter(apu);
                break;
            // 1/2
            case FC_CYCLES * 2:
                fc_quarter(apu);
                fc_half(apu);
                break;
            // 4/4 on divider false
            case FC_CYCLES * 4:
                if (!BIT_CHECK(apu->flags, AF_FC_DIVIDER)) {
                    BIT_SET_IF(apu->cpu->irq, IRQ_APU_FRAME,
                               !BIT_CHECK(apu->flags, AF_FC_IRQ_DISABLE));
                    fc_quarter(apu);
                    fc_half(apu);
                    apu->fc_timer = -1;
                }
                break;
            // 4/4 (5/4) on divider true
            case FC_CYCLES * 5:
                fc_quarter(apu);
                fc_half(apu);
                apu->fc_timer = -1;
                break;
        }
    }
    
    // Advance channel timers
    const ChannelIndex ch_timered[] = {
        CH_PULSE_1, CH_PULSE_2,
        CH_TRIANGLE, CH_TRIANGLE, // triangle runs at double rate
        CH_NOISE,
    };
    for (int i = 0; i < (sizeof(ch_timered) / sizeof(int)); i++) {
        ChannelIndex n = ch_timered[i];
        WaveformChannel *ch = apu->channels + n;
        if (ch->timer) {
            --ch->timer;
        } else {
            ch->timer = ch->timer_load;
            if (n == CH_NOISE) {
                int mode = !!BIT_CHECK(ch->flags, CHF_NOISE_MODE) * 5 + 1;
                uint16_t feedback =
                    ((ch->sequence & 1) ^ ((ch->sequence >> mode) & 1)) << 14;
                ch->sequence = (ch->sequence >> 1) | feedback;
            } else if ((n != CH_TRIANGLE) ||
                       (ch->length_counter && apu->linear_counter)) {
                ch->sequence = (ch->sequence + 1) % sequence_lengths[n];
            }
        }
    }
    
    // Advance delta modulation channel
    if (apu->dmc_timer) {
        --apu->dmc_timer;
    } else {
        apu->dmc_timer = apu->dmc_timer_load;
        if (!BIT_CHECK(apu->flags, AF_DMC_SILENT)) {
            if (apu->dmc_buffer & 1) {
                if (apu->dmc_delta <= 125) {
                    apu->dmc_delta += 2;
                }
            } else if (apu->dmc_delta >= 2) {
                apu->dmc_delta -= 2;
            }
        }
        apu->dmc_buffer >>= 1;
        if (apu->dmc_bit) {
            --apu->dmc_bit;
        } else {
            apu->dmc_bit = 8;
            if (apu->dmc_remain) {
                BIT_CLEAR(apu->flags, AF_DMC_SILENT);
                apu->dmc_buffer = mm_read(apu->cpu->mm, apu->dmc_addr++);
                --apu->dmc_remain;
                if (!apu->dmc_remain) {
                    if (BIT_CHECK(apu->flags, AF_DMC_LOOP)) {
                        apu->dmc_addr = apu->dmc_addr_load;
                        apu->dmc_remain = apu->dmc_length;
                    } else {
                        BIT_SET_IF(apu->cpu->irq, IRQ_APU_DMC,
                                   BIT_CHECK(apu->flags, AF_DMC_IRQ_ENABLE));
                    }
                }
            } else {
                BIT_SET(apu->flags, AF_DMC_SILENT);
            }
        }
    }
}

void apu_sample(APU *apu) {
    int pulse_out = 0;
    for (int i = 0; i < 2; i++) {
        WaveformChannel *p = apu->channels + i;
        pulse_out +=
            (!!p->length_counter
             && (p->timer_load >= 8) && (p->timer_load <= 0x7FF)
             && pulse_sequences[p->duty][p->sequence]) *
            (BIT_CHECK(p->flags, CHF_ENV_DISABLE) ? p->volume : p->env_decay);
    }
    
    WaveformChannel *t = apu->channels + CH_TRIANGLE;
    int triangle_out =
        !!t->length_counter * triangle_sequence[t->sequence];
    
    WaveformChannel *n = apu->channels + CH_NOISE;
    int noise_out = 2 *
        (!!n->length_counter && (n->sequence & 1)) *
        (BIT_CHECK(n->flags, CHF_ENV_DISABLE) ? n->volume : n->env_decay);
    
    apu->audio_buffer[(*apu->audio_pos)++] = pulse_mix[pulse_out]
                    + tnd_mix[triangle_out + noise_out + apu->dmc_delta];
    *apu->audio_pos %= 8192;
}
