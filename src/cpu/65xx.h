#ifndef cpu_65xx_h
#define cpu_65xx_h

#include "../common.h"

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
typedef struct CPU65xx CPU65xx;
typedef struct Opcode Opcode;

typedef uint8_t (*CPU65xxReadFuncPtr)(void *, uint16_t);
typedef void (*CPU65xxWriteFuncPtr)(void *, uint16_t, uint8_t);

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

typedef int (*OpcodeFunc)(CPU65xx *, const Opcode *, OpParam);

struct Opcode {
    const char *name;
    uint8_t *reg1;
    uint8_t *reg2;
    int cycles;
    OpcodeFunc func;
    AddressingMode am;
};

struct CPU65xx {
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
    // Memory I/O
    void *mm;
    CPU65xxReadFuncPtr read_func;
    CPU65xxWriteFuncPtr write_func;
    // Interrupt lines
    bool nmi;
    int irq;
    // Opcode lookup table
    Opcode opcodes[0x100];
};

void cpu_65xx_init(CPU65xx *cpu, void *mm, CPU65xxReadFuncPtr read_func,
                                           CPU65xxWriteFuncPtr write_func);

int cpu_65xx_step(CPU65xx *cpu, bool verbose);
int cpu_65xx_reset(CPU65xx *cpu, bool verbose);

void cpu_65xx_debug_print_state(CPU65xx *cpu);

#endif /* cpu_65xx_h */
