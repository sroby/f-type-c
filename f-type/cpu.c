#include "cpu.h"

// MISC. //

static void apply_page_boundary_penalty(CPUState *st, uint16_t a, uint16_t b) {
    if ((a << 4) != (b << 4)) {
        (st->t)++;
    }
}

// P.STATUS REGISTER //

static bool get_p_flag(CPUState *st, int flag) {
    return st->p & (1 << flag);
}

static void set_p_flag(CPUState *st, int flag, bool value) {
    if (value) {
        st->p |= (1 << flag);
    } else {
        st->p &= ~(1 << flag);
    }
}

static void apply_p_nz(CPUState *st, uint8_t value) {
    set_p_flag(st, P_Z, !value);
    set_p_flag(st, P_N, value & (1 << 7));
}

// STACK REGISTER //

static uint16_t get_stack_addr(CPUState *st) {
    return 0x100 + (uint16_t)(st->s);
}

static void stack_push(CPUState *st, uint8_t value) {
    mm_write(st->mm, get_stack_addr(st), value);
    (st->s)--;
}
static void stack_push_word(CPUState *st, uint16_t value) {
    stack_push(st, value & 0xff);
    stack_push(st, value >> 8);
}

static uint8_t stack_pull(CPUState *st) {
    (st->s)++;
    return mm_read(st->mm, get_stack_addr(st));
}
static uint16_t stack_pull_word(CPUState *st) {
    uint16_t value = (uint16_t)stack_pull(st) << 8;
    return value + (uint16_t)stack_pull(st);
}

// INTERRUPT HANDLING //

static int interrupt(CPUState *st, bool b_flag, uint16_t ivt_addr) {
    set_p_flag(st, P_B, b_flag);
    set_p_flag(st, P_I, true);
    if (ivt_addr != IVT_RESET) {
        stack_push_word(st, st->pc);
        stack_push(st, st->p);
    }
    st->pc = mm_read_word(st->mm, ivt_addr);
    return 7;
}

// OPCODES //

static uint8_t get_param_value(CPUState *st, const Opcode *op, OpParam param) {
    if (op->am == AM_IMMEDIATE) {
        return param.immediate_value;
    }
    return mm_read(st->mm, param.addr);
}

static void op_T(CPUState *st, const Opcode *op, OpParam param) {
    *op->reg2 = *op->reg1;
    if (op->reg2 != &st->s) {
        apply_p_nz(st, *op->reg2);
    }
}

static void op_LD(CPUState *st, const Opcode *op, OpParam param) {
    *op->reg1 = get_param_value(st, op, param);
    apply_p_nz(st, *op->reg1);
}

static void op_ST(CPUState *st, const Opcode *op, OpParam param) {
    mm_write(st->mm, param.addr, *op->reg1);
}

static void op_PH(CPUState *st, const Opcode *op, OpParam param) {
    uint8_t value = *op->reg1;
    if (op->reg1 == &st->p) {
        value |= (1 << P_B) + (1 << P__);
    }
    stack_push(st, value);
}

static void op_PL(CPUState *st, const Opcode *op, OpParam param) {
    *op->reg1 = stack_pull(st);
    if (op->reg1 == &st->p) {
        *op->reg1 &= ~((1 << P_B) + (1 << P__));
    } else {
        apply_p_nz(st, *op->reg1);
    }
}

static void op_ADC(CPUState *st, const Opcode *op, OpParam param) {
    uint8_t value = get_param_value(st, op, param);
    uint8_t carry = (get_p_flag(st, P_C) ? 1 : 0);
    set_p_flag(st, P_C, ((int)(st->a) + (int)carry + (int)value) >= 0x100);
    uint8_t result = st->a + carry + value;
    set_p_flag(st, P_V, (result & (1 << 7)) != (st->a & (1 << 7)));
    st->a = result;
    apply_p_nz(st, st->a);
}

static void op_SBC(CPUState *st, const Opcode *op, OpParam param) {
    uint8_t value = get_param_value(st, op, param);
    uint8_t carry = (get_p_flag(st, P_C) ? 1 : 0);
    set_p_flag(st, P_C, ((int)(st->a) + (int)carry - 1 - (int)value) >= 0);
    uint8_t result = st->a + carry - 1 - value;
    set_p_flag(st, P_V, (result & (1 << 7)) != (st->a & (1 << 7)));
    st->a = result;
    apply_p_nz(st, st->a);
}

static void op_AND(CPUState *st, const Opcode *op, OpParam param) {
    st->a &= get_param_value(st, op, param);
    apply_p_nz(st, st->a);
}

static void op_EOR(CPUState *st, const Opcode *op, OpParam param) {
    st->a ^= get_param_value(st, op, param);
    apply_p_nz(st, st->a);
}

static void op_ORA(CPUState *st, const Opcode *op, OpParam param) {
    st->a |= get_param_value(st, op, param);
    apply_p_nz(st, st->a);
}

static void op_CMP(CPUState *st, const Opcode *op, OpParam param) {
    uint8_t value = get_param_value(st, op, param);
    set_p_flag(st, P_C, ((int)(*op->reg1) - (int)value) >= 0);
    apply_p_nz(st, *op->reg1 - value);
}

static void op_BIT(CPUState *st, const Opcode *op, OpParam param) {
    uint8_t value = get_param_value(st, op, param);
    set_p_flag(st, P_Z, !(st->a & value));
    set_p_flag(st, P_N, value & (1 << 7));
    set_p_flag(st, P_V, value & (1 << 6));
}

static void op_INC(CPUState *st, const Opcode *op, OpParam param) {
    uint8_t result = get_param_value(st, op, param) + 1;
    mm_write(st->mm, param.addr, result);
    apply_p_nz(st, result);
}

static void op_IN(CPUState *st, const Opcode *op, OpParam param) {
    apply_p_nz(st, ++(*op->reg1));
}

static void op_DEC(CPUState *st, const Opcode *op, OpParam param) {
    uint8_t result = get_param_value(st, op, param) - 1;
    mm_write(st->mm, param.addr, result);
    apply_p_nz(st, result);
}

static void op_DE(CPUState *st, const Opcode *op, OpParam param) {
    apply_p_nz(st, --(*op->reg1));
}

static void shift_left(CPUState *st, const Opcode *op, OpParam param, uint8_t carry) {
    if (op->reg1) {
        set_p_flag(st, P_C, *op->reg1 & (1 << 7));
        *op->reg1 <<= 1;
        *op->reg1 += carry;
        apply_p_nz(st, *op->reg1);
    } else {
        uint8_t value = get_param_value(st, op, param);
        set_p_flag(st, P_C, value & (1 << 7));
        value <<= 1;
        value += carry;
        mm_write(st->mm, param.addr, value);
        apply_p_nz(st, value);
    }
}
static void op_ASL(CPUState *st, const Opcode *op, OpParam param) {
    shift_left(st, op, param, 0);
}
static void op_ROL(CPUState *st, const Opcode *op, OpParam param) {
    shift_left(st, op, param, (get_p_flag(st, P_C) ? 1 : 0));
}

static void shift_right(CPUState *st, const Opcode *op, OpParam param, uint8_t carry) {
    if (op->reg1) {
        set_p_flag(st, P_C, *op->reg1 & 1);
        *op->reg1 >>= 1;
        *op->reg1 += carry;
        apply_p_nz(st, *op->reg1);
    } else {
        uint8_t value = get_param_value(st, op, param);
        set_p_flag(st, P_C, value & 1);
        value >>= 1;
        value += carry;
        mm_write(st->mm, param.addr, value);
        apply_p_nz(st, value);
    }
}
static void op_LSR(CPUState *st, const Opcode *op, OpParam param) {
    shift_right(st, op, param, 0);
}
static void op_ROR(CPUState *st, const Opcode *op, OpParam param) {
    shift_right(st, op, param, (get_p_flag(st, P_C) ? 1 << 7 : 0));
}

static void op_JMP(CPUState *st, const Opcode *op, OpParam param) {
    st->pc = param.addr;
}

static void op_JSR(CPUState *st, const Opcode *op, OpParam param) {
    stack_push_word(st, st->pc);
    st->pc = param.addr;
}

static void op_RTI(CPUState *st, const Opcode *op, OpParam param) {
    st->s = stack_pull(st) & ~((1 << P_B) + (1 << P__));
    st->pc = stack_pull_word(st);
}

static void op_RTS(CPUState *st, const Opcode *op, OpParam param) {
    st->pc = stack_pull_word(st); // + 1 ??
}

static void cond_branch(CPUState *st, OpParam param, int flag, bool value) {
    if (get_p_flag(st, flag) != value) {
        return;
    }
    st->t++;
    uint16_t new_pc = st->pc + param.relative_addr;
    apply_page_boundary_penalty(st, st->pc, new_pc);
    st->pc = new_pc;
}
static void op_BPL(CPUState *st, const Opcode *op, OpParam param) {
    cond_branch(st, param, P_N, false);
}
static void op_BMI(CPUState *st, const Opcode *op, OpParam param) {
    cond_branch(st, param, P_N, true);
}
static void op_BVC(CPUState *st, const Opcode *op, OpParam param) {
    cond_branch(st, param, P_V, false);
}
static void op_BVS(CPUState *st, const Opcode *op, OpParam param) {
    cond_branch(st, param, P_V, true);
}
static void op_BCC(CPUState *st, const Opcode *op, OpParam param) {
    cond_branch(st, param, P_C, false);
}
static void op_BCS(CPUState *st, const Opcode *op, OpParam param) {
    cond_branch(st, param, P_C, true);
}
static void op_BNE(CPUState *st, const Opcode *op, OpParam param) {
    cond_branch(st, param, P_Z, false);
}
static void op_BEQ(CPUState *st, const Opcode *op, OpParam param) {
    cond_branch(st, param, P_Z, true);
}

static void op_BRK(CPUState *st, const Opcode *op, OpParam param) {
    st->pc++;
    st->t += interrupt(st, true, IVT_IRQ);
}

static void op_CLC(CPUState *st, const Opcode *op, OpParam param) {
    set_p_flag(st, P_C, false);
}
static void op_CLI(CPUState *st, const Opcode *op, OpParam param) {
    set_p_flag(st, P_I, false);
}
static void op_CLD(CPUState *st, const Opcode *op, OpParam param) {
    set_p_flag(st, P_D, false);
}
static void op_CLV(CPUState *st, const Opcode *op, OpParam param) {
    set_p_flag(st, P_V, false);
}
static void op_SEC(CPUState *st, const Opcode *op, OpParam param) {
    set_p_flag(st, P_C, true);
}
static void op_SEI(CPUState *st, const Opcode *op, OpParam param) {
    set_p_flag(st, P_I, true);
}
static void op_SED(CPUState *st, const Opcode *op, OpParam param) {
    set_p_flag(st, P_D, true);
}

// PUBLIC FUNCTIONS //

void cpu_init(CPUState *st, MemoryMap *mm) {
    st->a = st->x = st->y = 0;
    st->s = 0xff;
    st->p = 1 << P__;
    st->pc = 0;
    st->t = 0;
    
    st->mm = mm;
    
    // Initialize name on all opcodes so we can detect illegal usage
    for (int i = 0; i < 0x100; i++) {
        st->opcodes[i].name = NULL;
    }
    
    uint8_t *a = &st->a;
    uint8_t *x = &st->x;
    uint8_t *y = &st->y;
    uint8_t *s = &st->s;
    uint8_t *p = &st->p;
    
    // Define all legal opcodes
    st->opcodes[0xA8] = (Opcode) {"TAY", a, y, 2, op_T, AM_IMPLIED};
    st->opcodes[0xAA] = (Opcode) {"TAX", a, x, 2, op_T, AM_IMPLIED};
    st->opcodes[0xBA] = (Opcode) {"TSX", s, x, 2, op_T, AM_IMPLIED};
    st->opcodes[0x98] = (Opcode) {"TYA", y, a, 2, op_T, AM_IMPLIED};
    st->opcodes[0x8A] = (Opcode) {"TXA", x, a, 2, op_T, AM_IMPLIED};
    st->opcodes[0x9A] = (Opcode) {"TXS", x, s, 2, op_T, AM_IMPLIED};
    st->opcodes[0xA9] = (Opcode) {"LDA", a, 0, 2, op_LD, AM_IMMEDIATE};
    st->opcodes[0xA2] = (Opcode) {"LDX", x, 0, 2, op_LD, AM_IMMEDIATE};
    st->opcodes[0xA0] = (Opcode) {"LDY", y, 0, 2, op_LD, AM_IMMEDIATE};
    
    st->opcodes[0xA5] = (Opcode) {"LDA", a, 0, 3, op_LD, AM_ZP};
    st->opcodes[0xB5] = (Opcode) {"LDA", a, x, 4, op_LD, AM_ZP};
    st->opcodes[0xAD] = (Opcode) {"LDA", a, 0, 4, op_LD, AM_ABSOLUTE};
    st->opcodes[0xBD] = (Opcode) {"LDA", a, x, -4, op_LD, AM_ABSOLUTE};
    st->opcodes[0xB9] = (Opcode) {"LDA", a, y, -4, op_LD, AM_ABSOLUTE};
    st->opcodes[0xA1] = (Opcode) {"LDA", a, 0, 6, op_LD, AM_INDIRECT_X};
    st->opcodes[0xB1] = (Opcode) {"LDA", a, 0, -5, op_LD, AM_INDIRECT_Y};
    st->opcodes[0xA6] = (Opcode) {"LDX", x, 0, 3, op_LD, AM_ZP};
    st->opcodes[0xB6] = (Opcode) {"LDX", x, y, 4, op_LD, AM_ZP};
    st->opcodes[0xAE] = (Opcode) {"LDX", x, 0, 4, op_LD, AM_ABSOLUTE};
    st->opcodes[0xBE] = (Opcode) {"LDX", x, y, -4, op_LD, AM_ABSOLUTE};
    st->opcodes[0xA4] = (Opcode) {"LDY", y, 0, 3, op_LD, AM_ZP};
    st->opcodes[0xB4] = (Opcode) {"LDY", y, x, 4, op_LD, AM_ZP};
    st->opcodes[0xAC] = (Opcode) {"LDY", y, 0, 4, op_LD, AM_ABSOLUTE};
    st->opcodes[0xBC] = (Opcode) {"LDY", y, x, -4, op_LD, AM_ABSOLUTE};
    
    st->opcodes[0x85] = (Opcode) {"STA", a, 0, 3, op_ST, AM_ZP};
    st->opcodes[0x95] = (Opcode) {"STA", a, x, 4, op_ST, AM_ZP};
    st->opcodes[0x8D] = (Opcode) {"STA", a, 0, 4, op_ST, AM_ABSOLUTE};
    st->opcodes[0x9D] = (Opcode) {"STA", a, x, 5, op_ST, AM_ABSOLUTE};
    st->opcodes[0x99] = (Opcode) {"STA", a, y, 5, op_ST, AM_ABSOLUTE};
    st->opcodes[0x81] = (Opcode) {"STA", a, 0, 6, op_ST, AM_INDIRECT_X};
    st->opcodes[0x91] = (Opcode) {"STA", a, 0, 6, op_ST, AM_INDIRECT_Y};
    st->opcodes[0x86] = (Opcode) {"STX", x, 0, 3, op_ST, AM_ZP};
    st->opcodes[0x96] = (Opcode) {"STX", x, y, 4, op_ST, AM_ZP};
    st->opcodes[0x8E] = (Opcode) {"STX", x, 0, 4, op_ST, AM_ABSOLUTE};
    st->opcodes[0x84] = (Opcode) {"STY", y, 0, 3, op_ST, AM_ZP};
    st->opcodes[0x94] = (Opcode) {"STY", y, x, 4, op_ST, AM_ZP};
    st->opcodes[0x8C] = (Opcode) {"STY", y, 0, 4, op_ST, AM_ABSOLUTE};
    
    st->opcodes[0x48] = (Opcode) {"PHA", a, 0, 3, op_PH, AM_IMPLIED};
    st->opcodes[0x08] = (Opcode) {"PHP", p, 0, 3, op_PH, AM_IMPLIED};
    st->opcodes[0x68] = (Opcode) {"PLA", a, 0, 4, op_PL, AM_IMPLIED};
    st->opcodes[0x28] = (Opcode) {"PLP", p, 0, 4, op_PL, AM_IMPLIED};
    
    st->opcodes[0x69] = (Opcode) {"ADC", 0, 0, 2, op_ADC, AM_IMMEDIATE};
    st->opcodes[0x65] = (Opcode) {"ADC", 0, 0, 3, op_ADC, AM_ZP};
    st->opcodes[0x75] = (Opcode) {"ADC", 0, x, 4, op_ADC, AM_ZP};
    st->opcodes[0x6D] = (Opcode) {"ADC", 0, 0, 4, op_ADC, AM_ABSOLUTE};
    st->opcodes[0x7D] = (Opcode) {"ADC", 0, x, -4, op_ADC, AM_ABSOLUTE};
    st->opcodes[0x79] = (Opcode) {"ADC", 0, y, -4, op_ADC, AM_ABSOLUTE};
    st->opcodes[0x61] = (Opcode) {"ADC", 0, 0, 6, op_ADC, AM_INDIRECT_X};
    st->opcodes[0x71] = (Opcode) {"ADC", 0, 0, -5, op_ADC, AM_INDIRECT_Y};
    
    st->opcodes[0xE9] = (Opcode) {"SBC", 0, 0, 2, op_SBC, AM_IMMEDIATE};
    st->opcodes[0xE5] = (Opcode) {"SBC", 0, 0, 3, op_SBC, AM_ZP};
    st->opcodes[0xF5] = (Opcode) {"SBC", 0, x, 4, op_SBC, AM_ZP};
    st->opcodes[0xED] = (Opcode) {"SBC", 0, 0, 4, op_SBC, AM_ABSOLUTE};
    st->opcodes[0xFD] = (Opcode) {"SBC", 0, x, -4, op_SBC, AM_ABSOLUTE};
    st->opcodes[0xF9] = (Opcode) {"SBC", 0, y, -4, op_SBC, AM_ABSOLUTE};
    st->opcodes[0xE1] = (Opcode) {"SBC", 0, 0, 6, op_SBC, AM_INDIRECT_X};
    st->opcodes[0xF1] = (Opcode) {"SBC", 0, 0, -5, op_SBC, AM_INDIRECT_Y};
    
    st->opcodes[0x29] = (Opcode) {"AND", 0, 0, 2, op_AND, AM_IMMEDIATE};
    st->opcodes[0x25] = (Opcode) {"AND", 0, 0, 3, op_AND, AM_ZP};
    st->opcodes[0x35] = (Opcode) {"AND", 0, x, 4, op_AND, AM_ZP};
    st->opcodes[0x2D] = (Opcode) {"AND", 0, 0, 4, op_AND, AM_ABSOLUTE};
    st->opcodes[0x3D] = (Opcode) {"AND", 0, x, -4, op_AND, AM_ABSOLUTE};
    st->opcodes[0x39] = (Opcode) {"AND", 0, y, -4, op_AND, AM_ABSOLUTE};
    st->opcodes[0x21] = (Opcode) {"AND", 0, 0, 6, op_AND, AM_INDIRECT_X};
    st->opcodes[0x31] = (Opcode) {"AND", 0, 0, -5, op_AND, AM_INDIRECT_Y};
    
    st->opcodes[0x49] = (Opcode) {"EOR", 0, 0, 2, op_EOR, AM_IMMEDIATE};
    st->opcodes[0x45] = (Opcode) {"EOR", 0, 0, 3, op_EOR, AM_ZP};
    st->opcodes[0x55] = (Opcode) {"EOR", 0, x, 4, op_EOR, AM_ZP};
    st->opcodes[0x4D] = (Opcode) {"EOR", 0, 0, 4, op_EOR, AM_ABSOLUTE};
    st->opcodes[0x5D] = (Opcode) {"EOR", 0, x, -4, op_EOR, AM_ABSOLUTE};
    st->opcodes[0x59] = (Opcode) {"EOR", 0, y, -4, op_EOR, AM_ABSOLUTE};
    st->opcodes[0x41] = (Opcode) {"EOR", 0, 0, 6, op_EOR, AM_INDIRECT_X};
    st->opcodes[0x51] = (Opcode) {"EOR", 0, 0, -5, op_EOR, AM_INDIRECT_Y};
    
    st->opcodes[0x09] = (Opcode) {"ORA", 0, 0, 2, op_ORA, AM_IMMEDIATE};
    st->opcodes[0x05] = (Opcode) {"ORA", 0, 0, 3, op_ORA, AM_ZP};
    st->opcodes[0x15] = (Opcode) {"ORA", 0, x, 4, op_ORA, AM_ZP};
    st->opcodes[0x0D] = (Opcode) {"ORA", 0, 0, 4, op_ORA, AM_ABSOLUTE};
    st->opcodes[0x1D] = (Opcode) {"ORA", 0, x, -4, op_ORA, AM_ABSOLUTE};
    st->opcodes[0x19] = (Opcode) {"ORA", 0, y, -4, op_ORA, AM_ABSOLUTE};
    st->opcodes[0x01] = (Opcode) {"ORA", 0, 0, 6, op_ORA, AM_INDIRECT_X};
    st->opcodes[0x11] = (Opcode) {"ORA", 0, 0, -5, op_ORA, AM_INDIRECT_Y};
    
    st->opcodes[0xC9] = (Opcode) {"CMP", a, 0, 2, op_CMP, AM_IMMEDIATE};
    st->opcodes[0xC5] = (Opcode) {"CMP", a, 0, 3, op_CMP, AM_ZP};
    st->opcodes[0xD5] = (Opcode) {"CMP", a, x, 4, op_CMP, AM_ZP};
    st->opcodes[0xCD] = (Opcode) {"CMP", a, 0, 4, op_CMP, AM_ABSOLUTE};
    st->opcodes[0xDD] = (Opcode) {"CMP", a, x, -4, op_CMP, AM_ABSOLUTE};
    st->opcodes[0xD9] = (Opcode) {"CMP", a, y, -4, op_CMP, AM_ABSOLUTE};
    st->opcodes[0xC1] = (Opcode) {"CMP", a, 0, 6, op_CMP, AM_INDIRECT_X};
    st->opcodes[0xD1] = (Opcode) {"CMP", a, 0, -5, op_CMP, AM_INDIRECT_Y};
    st->opcodes[0xE0] = (Opcode) {"CPX", x, 0, 2, op_CMP, AM_IMMEDIATE};
    st->opcodes[0xE4] = (Opcode) {"CPX", x, 0, 3, op_CMP, AM_ZP};
    st->opcodes[0xEC] = (Opcode) {"CPX", x, 0, 4, op_CMP, AM_ABSOLUTE};
    st->opcodes[0xC0] = (Opcode) {"CPY", y, 0, 2, op_CMP, AM_IMMEDIATE};
    st->opcodes[0xC4] = (Opcode) {"CPY", y, 0, 3, op_CMP, AM_ZP};
    st->opcodes[0xCC] = (Opcode) {"CPY", y, 0, 4, op_CMP, AM_ABSOLUTE};
    
    st->opcodes[0x24] = (Opcode) {"BIT", 0, 0, 3, op_BIT, AM_ZP};
    st->opcodes[0x2C] = (Opcode) {"BIT", 0, 0, 4, op_BIT, AM_ABSOLUTE};
    
    st->opcodes[0xE6] = (Opcode) {"INC", 0, 0, 5, op_INC, AM_ZP};
    st->opcodes[0xF6] = (Opcode) {"INC", 0, x, 6, op_INC, AM_ZP};
    st->opcodes[0xEE] = (Opcode) {"INC", 0, 0, 6, op_INC, AM_ABSOLUTE};
    st->opcodes[0xFE] = (Opcode) {"INC", 0, x, 7, op_INC, AM_ABSOLUTE};
    st->opcodes[0xE8] = (Opcode) {"INX", x, 0, 2, op_IN, AM_IMPLIED};
    st->opcodes[0xC8] = (Opcode) {"INY", y, 0, 2, op_IN, AM_IMPLIED};
    
    st->opcodes[0xC6] = (Opcode) {"DEC", 0, 0, 5, op_DEC, AM_ZP};
    st->opcodes[0xD6] = (Opcode) {"DEC", 0, x, 6, op_DEC, AM_ZP};
    st->opcodes[0xCE] = (Opcode) {"DEC", 0, 0, 6, op_DEC, AM_ABSOLUTE};
    st->opcodes[0xDE] = (Opcode) {"DEC", 0, x, 7, op_DEC, AM_ABSOLUTE};
    st->opcodes[0xCA] = (Opcode) {"DEX", x, 0, 2, op_DE, AM_IMPLIED};
    st->opcodes[0x88] = (Opcode) {"DEY", y, 0, 2, op_DE, AM_IMPLIED};
    
    st->opcodes[0x0A] = (Opcode) {"ASL A", a, 0, 2, op_ASL, AM_IMPLIED};
    st->opcodes[0x06] = (Opcode) {"ASL", 0, 0, 5, op_ASL, AM_ZP};
    st->opcodes[0x16] = (Opcode) {"ASL", 0, x, 6, op_ASL, AM_ZP};
    st->opcodes[0x0E] = (Opcode) {"ASL", 0, 0, 6, op_ASL, AM_ABSOLUTE};
    st->opcodes[0x1E] = (Opcode) {"ASL", 0, x, 7, op_ASL, AM_ABSOLUTE};
    
    st->opcodes[0x0A] = (Opcode) {"LSR A", a, 0, 2, op_LSR, AM_IMPLIED};
    st->opcodes[0x06] = (Opcode) {"LSR", 0, 0, 5, op_LSR, AM_ZP};
    st->opcodes[0x16] = (Opcode) {"LSR", 0, x, 6, op_LSR, AM_ZP};
    st->opcodes[0x0E] = (Opcode) {"LSR", 0, 0, 6, op_LSR, AM_ABSOLUTE};
    st->opcodes[0x1E] = (Opcode) {"LSR", 0, x, 7, op_LSR, AM_ABSOLUTE};
    
    st->opcodes[0x0A] = (Opcode) {"ROL A", a, 0, 2, op_ROL, AM_IMPLIED};
    st->opcodes[0x06] = (Opcode) {"ROL", 0, 0, 5, op_ROL, AM_ZP};
    st->opcodes[0x16] = (Opcode) {"ROL", 0, x, 6, op_ROL, AM_ZP};
    st->opcodes[0x0E] = (Opcode) {"ROL", 0, 0, 6, op_ROL, AM_ABSOLUTE};
    st->opcodes[0x1E] = (Opcode) {"ROL", 0, x, 7, op_ROL, AM_ABSOLUTE};
    
    st->opcodes[0x0A] = (Opcode) {"ROR A", a, 0, 2, op_ROR, AM_IMPLIED};
    st->opcodes[0x06] = (Opcode) {"ROR", 0, 0, 5, op_ROR, AM_ZP};
    st->opcodes[0x16] = (Opcode) {"ROR", 0, x, 6, op_ROR, AM_ZP};
    st->opcodes[0x0E] = (Opcode) {"ROR", 0, 0, 6, op_ROR, AM_ABSOLUTE};
    st->opcodes[0x1E] = (Opcode) {"ROR", 0, x, 7, op_ROR, AM_ABSOLUTE};
    
    st->opcodes[0x4C] = (Opcode) {"JMP", 0, 0, 3, op_JMP, AM_ABSOLUTE};
    st->opcodes[0x6C] = (Opcode) {"JMP", 0, 0, 5, op_JMP, AM_INDIRECT_WORD};
    st->opcodes[0x20] = (Opcode) {"JSR", 0, 0, 6, op_JSR, AM_ABSOLUTE};
    st->opcodes[0x40] = (Opcode) {"RTI", 0, 0, 6, op_RTI, AM_IMPLIED};
    st->opcodes[0x60] = (Opcode) {"RTS", 0, 0, 6, op_RTS, AM_IMPLIED};
    
    st->opcodes[0x10] = (Opcode) {"BPL", 0, 0, 2, op_BPL, AM_RELATIVE};
    st->opcodes[0x30] = (Opcode) {"BMI", 0, 0, 2, op_BMI, AM_RELATIVE};
    st->opcodes[0x50] = (Opcode) {"BVC", 0, 0, 2, op_BVC, AM_RELATIVE};
    st->opcodes[0x70] = (Opcode) {"BVS", 0, 0, 2, op_BVS, AM_RELATIVE};
    st->opcodes[0x90] = (Opcode) {"BCC", 0, 0, 2, op_BCC, AM_RELATIVE};
    st->opcodes[0xB0] = (Opcode) {"BCS", 0, 0, 2, op_BCS, AM_RELATIVE};
    st->opcodes[0xD0] = (Opcode) {"BNE", 0, 0, 2, op_BNE, AM_RELATIVE};
    st->opcodes[0xF0] = (Opcode) {"BEQ", 0, 0, 2, op_BEQ, AM_RELATIVE};
    
    st->opcodes[0x00] = (Opcode) {"BRK", 0, 0, 0, op_BRK, AM_IMPLIED};
    
    st->opcodes[0x18] = (Opcode) {"CLC", 0, 0, 2, op_CLC, AM_IMPLIED};
    st->opcodes[0x58] = (Opcode) {"CLI", 0, 0, 2, op_CLI, AM_IMPLIED};
    st->opcodes[0xD8] = (Opcode) {"CLD", 0, 0, 2, op_CLD, AM_IMPLIED};
    st->opcodes[0xB8] = (Opcode) {"CLV", 0, 0, 2, op_CLV, AM_IMPLIED};
    st->opcodes[0x38] = (Opcode) {"SEC", 0, 0, 2, op_SEC, AM_IMPLIED};
    st->opcodes[0x78] = (Opcode) {"SEI", 0, 0, 2, op_SEI, AM_IMPLIED};
    st->opcodes[0xF8] = (Opcode) {"SED", 0, 0, 2, op_SED, AM_IMPLIED};

    st->opcodes[0xEA] = (Opcode) {"NOP", 0, 0, 2, NULL, AM_IMPLIED};
}

int cpu_step(CPUState *st, bool verbose) {
    // Fetch next instruction
    uint8_t inst = mm_read(st->mm, st->pc++);
    const Opcode *op = &st->opcodes[inst];
    if (!op->name) {
        printf("Invalid Opcode %d\n", inst);
        return -1;
    }
    
    // Fetch parameter, if any
    uint8_t zp_addr;
    uint16_t pre_indexing = 0;
    OpParam param;
    switch (op->am) {
        case AM_IMPLIED:
            param.addr = 0;
            break;
        case AM_IMMEDIATE:
            param.immediate_value = mm_read(st->mm, st->pc++);
            break;
        case AM_ZP:
            zp_addr = mm_read(st->mm, st->pc++);
            if (op->reg2) {
                zp_addr += *op->reg2;
            }
            param.addr = zp_addr;
            break;
        case AM_ABSOLUTE:
            pre_indexing = param.addr = mm_read_word(st->mm, st->pc);
            st->pc += 2;
            if (op->reg2) {
                param.addr += *op->reg2;
            }
            break;
        case AM_INDIRECT_WORD:
            param.addr = mm_read_word(st->mm, mm_read_word(st->mm, st->pc));
            st->pc += 2;
            break;
        case AM_INDIRECT_X:
            zp_addr = mm_read(st->mm, st->pc++) + st->x;
            param.addr = mm_read_word(st->mm, zp_addr);
            break;
        case AM_INDIRECT_Y:
            pre_indexing = mm_read_word(st->mm, mm_read(st->mm, st->pc++));
            param.addr = pre_indexing + st->y;
            break;
        case AM_RELATIVE:
            param.relative_addr = mm_read(st->mm, st->pc++);
            break;
    }
    
    if (op->cycles < 0) {
        st->t = abs(op->cycles);
        apply_page_boundary_penalty(st, pre_indexing, param.addr);
    } else {
        st->t = op->cycles;
    }
    
    if (verbose) {
        printf(" %s", op->name);
        switch (op->am) {
            case AM_IMPLIED:
                break;
            case AM_IMMEDIATE:
                printf(" #$%02x", param.immediate_value);
                break;
            case AM_ZP:
                printf(" $%02x", param.addr);
                break;
            case AM_ABSOLUTE:
                printf(" $%04x", param.addr);
                break;
            case AM_INDIRECT_WORD:
                printf(" ($%04x)", param.addr);
                break;
            case AM_INDIRECT_X:
                printf(" ($%02x,X)", param.addr);
                break;
            case AM_INDIRECT_Y:
                printf(" ($%02x),Y", param.addr);
                break;
            case AM_RELATIVE:
                printf(" %+d", param.relative_addr);
                break;
        }
        if (op->am == AM_ZP || op->am == AM_ABSOLUTE) {
            if (op->reg2 == &st->x) {
                printf(",X");
            } else if (op->reg2 == &st->y) {
                printf(",Y");
            }
        }
        printf("\n");
    }
    
    if (op->func) {
        (*op->func)(st, op, param);
    }
    
    return st->t;
}

int cpu_irq(CPUState *st) {
    if (get_p_flag(st, P_I)) {
        return 0;
    }
    return interrupt(st, false, IVT_IRQ);
}

int cpu_nmi(CPUState *st) {
    return interrupt(st, false, IVT_NMI);
}

int cpu_reset(CPUState *st) {
    return interrupt(st, true, IVT_RESET);
}

void cpu_debug_print_state(CPUState *st) {
    printf("PC=%04x A=%02x X=%02x Y=%02x P=%02x[", st->pc, st->a, st->x, st->y, st->p);
    for (int i = 0; i < 8; i++) {
        printf("%c", (st->p & (1 << i) ? "czidb-vn"[i] : '.'));
    }
    printf("] S=%02x{", st->s);
    for (int i = 0xff; i > st->s; i--) {
        printf(" %02x", st->mm->wram[0x100 + i]);
    }
    printf(" }\n");
}
