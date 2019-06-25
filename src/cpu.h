#ifndef cpu_h
#define cpu_h

#include "common.h"

// P flags
#define P_C 0 // Carry
#define P_Z 1 // Zero value
#define P_I 2 // IRQ disable
#define P_D 3 // Decimal mode (not supported)
#define P_B 4 // Break
#define P__ 5 // Unused
#define P_V 6 // Overflow
#define P_N 7 // Negative value

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
    uint64_t t;
    // Memory map
    MemoryMap *mm;
    // Opcode lookup table
    Opcode opcodes[0x100];
};

void cpu_init(CPUState *cpu, MemoryMap *mm);

int cpu_step(CPUState *cpu, bool verbose);

void cpu_irq(CPUState *cpu);
void cpu_nmi(CPUState *cpu);
void cpu_reset(CPUState *cpu);

void cpu_external_t_increment(CPUState *cpu, int amount);

void cpu_debug_print_state(CPUState *cpu);

#endif /* cpu_h */
