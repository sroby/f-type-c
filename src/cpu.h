#ifndef cpu_h
#define cpu_h

#include "common.h"

// P flags
typedef enum {
    P_C = 1 << 0, // Carry
    P_Z = 1 << 1, // Zero value
    P_I = 1 << 2, // IRQ disable
    P_D = 1 << 3, // Decimal mode (not supported)
    P_B = 1 << 4, // Break
    P__ = 1 << 5, // Unused
    P_V = 1 << 6, // Overflow
    P_N = 1 << 7  // Negative value
} PFlag;

// Interrupt Vector Table
#define IVT_NMI   0xFFFA
#define IVT_RESET 0xFFFC
#define IVT_IRQ   0xFFFE

// Forward declarations
typedef struct CPUState CPUState;
typedef struct Opcode Opcode;
typedef struct MemoryMap MemoryMap;

typedef enum {
    AM_IMPLIED,
    AM_IMMEDIATE,
    AM_ZP,
    AM_ABSOLUTE,
    AM_INDIRECT_WORD,
    AM_INDIRECT_X,
    AM_INDIRECT_Y,
    AM_RELATIVE
} AddressingMode;

typedef union {
    uint16_t addr;
    uint8_t immediate_value;
    int8_t relative_addr;
} OpParam;

struct Opcode {
    const char *name;
    uint8_t *reg1;
    uint8_t *reg2;
    int cycles;
    void (*func)(CPUState *, const Opcode *, OpParam);
    AddressingMode am;
};

struct CPUState {
    // General purpose registers
    uint8_t a;
    uint8_t x;
    uint8_t y;
    // Stack register
    uint8_t s;
    // Processor status register
    uint8_t p;
    // Program counter
    uint16_t pc;
    // Total cycle counter
    uint64_t time;
    // Memory map
    MemoryMap *mm;
    // Interrupt lines
    bool nmi;
    int irq;
    // Opcode lookup table
    Opcode opcodes[0x100];
};

void cpu_init(CPUState *cpu, MemoryMap *mm);

int cpu_step(CPUState *cpu, bool verbose);
void cpu_reset(CPUState *cpu, bool verbose);

void cpu_external_t_increment(CPUState *cpu, int amount);

void cpu_debug_print_state(CPUState *cpu);

#endif /* cpu_h */
