#ifndef f_apu_h
#define f_apu_h

#include "../common.h"

// Channel indexes
typedef enum {
    CH_PULSE_1 = 0,
    CH_PULSE_2,
    CH_TRIANGLE,
    CH_NOISE,
    CH_DMC,
} ChannelIndex;

// How many cycles in a quarter frame
#define FC_CYCLES 3728

// Channel flags
typedef enum {
    CHF_HALT = 0,
    CHF_ENV_DISABLE,
    CHF_ENV_START,
    CHF_NOISE_MODE,
    CHF_SWEEP_ENABLE,
    CHF_SWEEP_NEGATE,
    CHF_SWEEP_RELOAD,
} ChFlag;

// APU flags
typedef enum {
    AF_DMC_IRQ_ENABLE = 0,
    AF_DMC_LOOP,
    AF_DMC_SILENT,
    AF_FC_IRQ_DISABLE,
    AF_FC_DIVIDER,
    AF_LINEAR_COUNTER_RELOAD,
} APUFlag;

// Forward declarations
typedef struct CPU65xx CPU65xx;

typedef struct WaveformChannel {
    int flags;
    uint16_t sequence;      // Pulse/Triangle: current position in the sequencer
                            // Noise: shift register
    uint8_t volume;         // Pulse/Noise: current constant volume
                            //              OR envelope load
                            // Triangle: unused
    uint8_t env_divider;
    uint8_t env_decay;
    uint8_t duty;           // Pulse: duty
                            // Triangle/Noise: unused
    uint16_t timer;
    uint16_t timer_load;
    uint8_t length_counter;
    
    uint8_t sweep_counter;
    uint8_t sweep_counter_load;
    uint8_t sweep_shift;
} WaveformChannel;

typedef struct APU {
    int flags;
    
    CPU65xx *cpu;
    
    WaveformChannel channels[4];
    
    uint8_t ch_enabled;
    
    uint8_t linear_counter;
    uint8_t linear_counter_load;
    
    // DMC state
    uint16_t dmc_addr;
    uint16_t dmc_addr_load;
    uint16_t dmc_length;
    uint16_t dmc_remain;
    uint8_t dmc_bit;
    uint8_t dmc_buffer;
    uint8_t dmc_delta;
    uint16_t dmc_timer;
    uint16_t dmc_timer_load;
    
    // Frame counter
    int fc_timer;
    
    uint64_t time;
    
    int16_t *frame;
} APU;

void apu_init(APU *apu, CPU65xx *cpu, int16_t *frame);

void apu_step(APU *apu);
void apu_sample(APU *apu, int pos);

#endif /* f_apu_h */
