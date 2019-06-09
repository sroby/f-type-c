#include "cpu.h"

// P.STATUS REGISTER //

static bool get_p_flag(cpu_state *st, int flag) {
    return st->p & (1 << flag);
}

static void set_p_flag(cpu_state *st, int flag, bool value) {
    if (value) {
        st->p |= (1 << flag);
    } else {
        st->p &= ~(1 << flag);
    }
}

static void apply_p_nz(cpu_state *st, uint8_t value) {
    set_p_flag(st, P_Z, !value);
    set_p_flag(st, P_N, value & (1 << 7));
}

// STACK REGISTER //

static uint16_t get_stack_addr(cpu_state *st) {
    return 0x100 + (uint16_t)(st->s);
}

static void stack_push(cpu_state *st, uint8_t value) {
    mm_write(st->mm, get_stack_addr(st), value);
    (st->s)--;
}
static void stack_push_word(cpu_state *st, uint16_t value) {
    stack_push(st, value & 0xff);
    stack_push(st, value >> 8);
}

static uint8_t stack_pull(cpu_state *st) {
    (st->s)++;
    return mm_read(st->mm, get_stack_addr(st));
}
static uint16_t stack_pull_word(cpu_state *st) {
    uint16_t value = (uint16_t)stack_pull(st) << 8;
    return value + (uint16_t)stack_pull(st);
}

static void apply_page_boundary_penalty(cpu_state *st, uint16_t a, uint16_t b) {
    if ((a << 4) != (b << 4)) {
        (st->t)++;
    }
}

// INTERRUPT HANDLING //

static int interrupt(cpu_state *st, bool b_flag, uint16_t ivt_addr) {
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

static uint8_t get_param_value(cpu_state *st, const opcode *op, op_param param) {
    if (op->am == AM_IMMEDIATE) {
        return param.immediate_value;
    }
    return mm_read(st->mm, param.addr);
}

static void op_T(cpu_state *st, const opcode *op, op_param param) {
    *op->reg2 = *op->reg1;
    if (op->reg2 != &st->s) {
        apply_p_nz(st, *op->reg2);
    }
}

static void op_LD(cpu_state *st, const opcode *op, op_param param) {
    *op->reg1 = get_param_value(st, op, param);
    apply_p_nz(st, *op->reg1);
}

static void op_ST(cpu_state *st, const opcode *op, op_param param) {
    mm_write(st->mm, param.addr, *op->reg1);
}

static void op_PH(cpu_state *st, const opcode *op, op_param param) {
    uint8_t value = *op->reg1;
    if (op->reg1 == &st->p) {
        value |= (1 << P_B) + (1 << P__);
    }
    stack_push(st, value);
}

static void op_PL(cpu_state *st, const opcode *op, op_param param) {
    *op->reg1 = stack_pull(st);
    if (op->reg1 == &st->p) {
        *op->reg1 &= ~((1 << P_B) + (1 << P__));
    } else {
        apply_p_nz(st, *op->reg1);
    }
}

static void op_ADC(cpu_state *st, const opcode *op, op_param param) {
    uint8_t value = get_param_value(st, op, param);
    uint8_t carry = (get_p_flag(st, P_C) ? 1 : 0);
    set_p_flag(st, P_C, ((int)(st->a) + (int)carry + (int)value) >= 0x100);
    uint8_t result = st->a + carry + value;
    set_p_flag(st, P_V, (result & (1 << 7)) != (st->a & (1 << 7)));
    st->a = result;
    apply_p_nz(st, st->a);
}

static void op_SBC(cpu_state *st, const opcode *op, op_param param) {
    uint8_t value = get_param_value(st, op, param);
    uint8_t carry = (get_p_flag(st, P_C) ? 1 : 0);
    set_p_flag(st, P_C, ((int)(st->a) + (int)carry - 1 - (int)value) >= 0);
    uint8_t result = st->a + carry - 1 - value;
    set_p_flag(st, P_V, (result & (1 << 7)) != (st->a & (1 << 7)));
    st->a = result;
    apply_p_nz(st, st->a);
}

static void op_AND(cpu_state *st, const opcode *op, op_param param) {
    st->a &= get_param_value(st, op, param);
    apply_p_nz(st, st->a);
}

static void op_EOR(cpu_state *st, const opcode *op, op_param param) {
    st->a ^= get_param_value(st, op, param);
    apply_p_nz(st, st->a);
}

static void op_ORA(cpu_state *st, const opcode *op, op_param param) {
    st->a |= get_param_value(st, op, param);
    apply_p_nz(st, st->a);
}

static void op_CMP(cpu_state *st, const opcode *op, op_param param) {
    uint8_t value = get_param_value(st, op, param);
    set_p_flag(st, P_C, ((int)(*op->reg1) - (int)value) >= 0);
    apply_p_nz(st, *op->reg1 - value);
}

static void op_BIT(cpu_state *st, const opcode *op, op_param param) {
    uint8_t value = get_param_value(st, op, param);
    set_p_flag(st, P_Z, !(st->a & value));
    set_p_flag(st, P_N, value & (1 << 7));
    set_p_flag(st, P_V, value & (1 << 6));
}

static void op_INC(cpu_state *st, const opcode *op, op_param param) {
    uint8_t result = get_param_value(st, op, param) + 1;
    mm_write(st->mm, param.addr, result);
    apply_p_nz(st, result);
}

static void op_IN(cpu_state *st, const opcode *op, op_param param) {
    apply_p_nz(st, ++(*op->reg1));
}

static void op_DEC(cpu_state *st, const opcode *op, op_param param) {
    uint8_t result = get_param_value(st, op, param) - 1;
    mm_write(st->mm, param.addr, result);
    apply_p_nz(st, result);
}

static void op_DE(cpu_state *st, const opcode *op, op_param param) {
    apply_p_nz(st, --(*op->reg1));
}

static void shift_left(cpu_state *st, const opcode *op, op_param param, uint8_t carry) {
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
static void op_ASL(cpu_state *st, const opcode *op, op_param param) {
    shift_left(st, op, param, 0);
}
static void op_ROL(cpu_state *st, const opcode *op, op_param param) {
    shift_left(st, op, param, (get_p_flag(st, P_C) ? 1 : 0));
}

static void shift_right(cpu_state *st, const opcode *op, op_param param, uint8_t carry) {
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
static void op_LSR(cpu_state *st, const opcode *op, op_param param) {
    shift_right(st, op, param, 0);
}
static void op_ROR(cpu_state *st, const opcode *op, op_param param) {
    shift_right(st, op, param, (get_p_flag(st, P_C) ? 1 << 7 : 0));
}

static void op_JMP(cpu_state *st, const opcode *op, op_param param) {
    st->pc = param.addr;
}

static void op_JSR(cpu_state *st, const opcode *op, op_param param) {
    stack_push_word(st, st->pc);
    st->pc = param.addr;
}

static void op_RTI(cpu_state *st, const opcode *op, op_param param) {
    st->s = stack_pull(st) & ~((1 << P_B) + (1 << P__));
    st->pc = stack_pull_word(st);
}

static void op_RTS(cpu_state *st, const opcode *op, op_param param) {
    st->pc = stack_pull_word(st); // + 1 ??
}

static void cond_branch(cpu_state *st, op_param param, int flag, bool value) {
    if (get_p_flag(st, flag) != value) {
        return;
    }
    st->t++;
    uint16_t new_pc = st->pc + param.relative_addr;
    apply_page_boundary_penalty(st, st->pc, new_pc);
    st->pc = new_pc;
}
static void op_BPL(cpu_state *st, const opcode *op, op_param param) {
    cond_branch(st, param, P_N, false);
}
static void op_BMI(cpu_state *st, const opcode *op, op_param param) {
    cond_branch(st, param, P_N, true);
}
static void op_BVC(cpu_state *st, const opcode *op, op_param param) {
    cond_branch(st, param, P_V, false);
}
static void op_BVS(cpu_state *st, const opcode *op, op_param param) {
    cond_branch(st, param, P_V, true);
}
static void op_BCC(cpu_state *st, const opcode *op, op_param param) {
    cond_branch(st, param, P_C, false);
}
static void op_BCS(cpu_state *st, const opcode *op, op_param param) {
    cond_branch(st, param, P_C, true);
}
static void op_BNE(cpu_state *st, const opcode *op, op_param param) {
    cond_branch(st, param, P_Z, false);
}
static void op_BEQ(cpu_state *st, const opcode *op, op_param param) {
    cond_branch(st, param, P_Z, true);
}

static void op_BRK(cpu_state *st, const opcode *op, op_param param) {
    st->pc++;
    st->t += interrupt(st, true, IVT_IRQ);
}

static void op_CLC(cpu_state *st, const opcode *op, op_param param) {
    set_p_flag(st, P_C, false);
}
static void op_CLI(cpu_state *st, const opcode *op, op_param param) {
    set_p_flag(st, P_I, false);
}
static void op_CLD(cpu_state *st, const opcode *op, op_param param) {
    set_p_flag(st, P_D, false);
}
static void op_CLV(cpu_state *st, const opcode *op, op_param param) {
    set_p_flag(st, P_V, false);
}
static void op_SEC(cpu_state *st, const opcode *op, op_param param) {
    set_p_flag(st, P_C, true);
}
static void op_SEI(cpu_state *st, const opcode *op, op_param param) {
    set_p_flag(st, P_I, true);
}
static void op_SED(cpu_state *st, const opcode *op, op_param param) {
    set_p_flag(st, P_D, true);
}

// PUBLIC FUNCTIONS //

void cpu_init(cpu_state *st, memory_map *mm) {
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
    st->opcodes[0xA8] = (opcode) {"TAY", a, y, 2, op_T, AM_IMPLIED};
    st->opcodes[0xAA] = (opcode) {"TAX", a, x, 2, op_T, AM_IMPLIED};
    st->opcodes[0xBA] = (opcode) {"TSX", s, x, 2, op_T, AM_IMPLIED};
    st->opcodes[0x98] = (opcode) {"TYA", y, a, 2, op_T, AM_IMPLIED};
    st->opcodes[0x8A] = (opcode) {"TXA", x, a, 2, op_T, AM_IMPLIED};
    st->opcodes[0x9A] = (opcode) {"TXS", x, s, 2, op_T, AM_IMPLIED};
    st->opcodes[0xA9] = (opcode) {"LDA", a, 0, 2, op_LD, AM_IMMEDIATE};
    st->opcodes[0xA2] = (opcode) {"LDX", x, 0, 2, op_LD, AM_IMMEDIATE};
    st->opcodes[0xA0] = (opcode) {"LDY", y, 0, 2, op_LD, AM_IMMEDIATE};
    
    st->opcodes[0xA5] = (opcode) {"LDA", a, 0, 3, op_LD, AM_ZP};
    st->opcodes[0xB5] = (opcode) {"LDA", a, x, 4, op_LD, AM_ZP};
    st->opcodes[0xAD] = (opcode) {"LDA", a, 0, 4, op_LD, AM_ABSOLUTE};
    st->opcodes[0xBD] = (opcode) {"LDA", a, x, -4, op_LD, AM_ABSOLUTE};
    st->opcodes[0xB9] = (opcode) {"LDA", a, y, -4, op_LD, AM_ABSOLUTE};
    st->opcodes[0xA1] = (opcode) {"LDA", a, 0, 6, op_LD, AM_INDIRECT_X};
    st->opcodes[0xB1] = (opcode) {"LDA", a, 0, -5, op_LD, AM_INDIRECT_Y};
    st->opcodes[0xA6] = (opcode) {"LDX", x, 0, 3, op_LD, AM_ZP};
    st->opcodes[0xB6] = (opcode) {"LDX", x, y, 4, op_LD, AM_ZP};
    st->opcodes[0xAE] = (opcode) {"LDX", x, 0, 4, op_LD, AM_ABSOLUTE};
    st->opcodes[0xBE] = (opcode) {"LDX", x, y, -4, op_LD, AM_ABSOLUTE};
    st->opcodes[0xA4] = (opcode) {"LDY", y, 0, 3, op_LD, AM_ZP};
    st->opcodes[0xB4] = (opcode) {"LDY", y, x, 4, op_LD, AM_ZP};
    st->opcodes[0xAC] = (opcode) {"LDY", y, 0, 4, op_LD, AM_ABSOLUTE};
    st->opcodes[0xBC] = (opcode) {"LDY", y, x, -4, op_LD, AM_ABSOLUTE};
    
    st->opcodes[0x85] = (opcode) {"STA", a, 0, 3, op_ST, AM_ZP};
    st->opcodes[0x95] = (opcode) {"STA", a, x, 4, op_ST, AM_ZP};
    st->opcodes[0x8D] = (opcode) {"STA", a, 0, 4, op_ST, AM_ABSOLUTE};
    st->opcodes[0x9D] = (opcode) {"STA", a, x, 5, op_ST, AM_ABSOLUTE};
    st->opcodes[0x99] = (opcode) {"STA", a, y, 5, op_ST, AM_ABSOLUTE};
    st->opcodes[0x81] = (opcode) {"STA", a, 0, 6, op_ST, AM_INDIRECT_X};
    st->opcodes[0x91] = (opcode) {"STA", a, 0, 6, op_ST, AM_INDIRECT_Y};
    st->opcodes[0x86] = (opcode) {"STX", x, 0, 3, op_ST, AM_ZP};
    st->opcodes[0x96] = (opcode) {"STX", x, y, 4, op_ST, AM_ZP};
    st->opcodes[0x8E] = (opcode) {"STX", x, 0, 4, op_ST, AM_ABSOLUTE};
    st->opcodes[0x84] = (opcode) {"STY", y, 0, 3, op_ST, AM_ZP};
    st->opcodes[0x94] = (opcode) {"STY", y, x, 4, op_ST, AM_ZP};
    st->opcodes[0x8C] = (opcode) {"STY", y, 0, 4, op_ST, AM_ABSOLUTE};
    
    st->opcodes[0x48] = (opcode) {"PHA", a, 0, 3, op_PH, AM_IMPLIED};
    st->opcodes[0x08] = (opcode) {"PHP", p, 0, 3, op_PH, AM_IMPLIED};
    st->opcodes[0x68] = (opcode) {"PLA", a, 0, 4, op_PL, AM_IMPLIED};
    st->opcodes[0x28] = (opcode) {"PLP", p, 0, 4, op_PL, AM_IMPLIED};
    
    st->opcodes[0x69] = (opcode) {"ADC", 0, 0, 2, op_ADC, AM_IMMEDIATE};
    st->opcodes[0x65] = (opcode) {"ADC", 0, 0, 3, op_ADC, AM_ZP};
    st->opcodes[0x75] = (opcode) {"ADC", 0, x, 4, op_ADC, AM_ZP};
    st->opcodes[0x6D] = (opcode) {"ADC", 0, 0, 4, op_ADC, AM_ABSOLUTE};
    st->opcodes[0x7D] = (opcode) {"ADC", 0, x, -4, op_ADC, AM_ABSOLUTE};
    st->opcodes[0x79] = (opcode) {"ADC", 0, y, -4, op_ADC, AM_ABSOLUTE};
    st->opcodes[0x61] = (opcode) {"ADC", 0, 0, 6, op_ADC, AM_INDIRECT_X};
    st->opcodes[0x71] = (opcode) {"ADC", 0, 0, -5, op_ADC, AM_INDIRECT_Y};
    
    st->opcodes[0xE9] = (opcode) {"SBC", 0, 0, 2, op_SBC, AM_IMMEDIATE};
    st->opcodes[0xE5] = (opcode) {"SBC", 0, 0, 3, op_SBC, AM_ZP};
    st->opcodes[0xF5] = (opcode) {"SBC", 0, x, 4, op_SBC, AM_ZP};
    st->opcodes[0xED] = (opcode) {"SBC", 0, 0, 4, op_SBC, AM_ABSOLUTE};
    st->opcodes[0xFD] = (opcode) {"SBC", 0, x, -4, op_SBC, AM_ABSOLUTE};
    st->opcodes[0xF9] = (opcode) {"SBC", 0, y, -4, op_SBC, AM_ABSOLUTE};
    st->opcodes[0xE1] = (opcode) {"SBC", 0, 0, 6, op_SBC, AM_INDIRECT_X};
    st->opcodes[0xF1] = (opcode) {"SBC", 0, 0, -5, op_SBC, AM_INDIRECT_Y};
    
    st->opcodes[0x29] = (opcode) {"AND", 0, 0, 2, op_AND, AM_IMMEDIATE};
    st->opcodes[0x25] = (opcode) {"AND", 0, 0, 3, op_AND, AM_ZP};
    st->opcodes[0x35] = (opcode) {"AND", 0, x, 4, op_AND, AM_ZP};
    st->opcodes[0x2D] = (opcode) {"AND", 0, 0, 4, op_AND, AM_ABSOLUTE};
    st->opcodes[0x3D] = (opcode) {"AND", 0, x, -4, op_AND, AM_ABSOLUTE};
    st->opcodes[0x39] = (opcode) {"AND", 0, y, -4, op_AND, AM_ABSOLUTE};
    st->opcodes[0x21] = (opcode) {"AND", 0, 0, 6, op_AND, AM_INDIRECT_X};
    st->opcodes[0x31] = (opcode) {"AND", 0, 0, -5, op_AND, AM_INDIRECT_Y};
    
    st->opcodes[0x49] = (opcode) {"EOR", 0, 0, 2, op_EOR, AM_IMMEDIATE};
    st->opcodes[0x45] = (opcode) {"EOR", 0, 0, 3, op_EOR, AM_ZP};
    st->opcodes[0x55] = (opcode) {"EOR", 0, x, 4, op_EOR, AM_ZP};
    st->opcodes[0x4D] = (opcode) {"EOR", 0, 0, 4, op_EOR, AM_ABSOLUTE};
    st->opcodes[0x5D] = (opcode) {"EOR", 0, x, -4, op_EOR, AM_ABSOLUTE};
    st->opcodes[0x59] = (opcode) {"EOR", 0, y, -4, op_EOR, AM_ABSOLUTE};
    st->opcodes[0x41] = (opcode) {"EOR", 0, 0, 6, op_EOR, AM_INDIRECT_X};
    st->opcodes[0x51] = (opcode) {"EOR", 0, 0, -5, op_EOR, AM_INDIRECT_Y};
    
    st->opcodes[0x09] = (opcode) {"ORA", 0, 0, 2, op_ORA, AM_IMMEDIATE};
    st->opcodes[0x05] = (opcode) {"ORA", 0, 0, 3, op_ORA, AM_ZP};
    st->opcodes[0x15] = (opcode) {"ORA", 0, x, 4, op_ORA, AM_ZP};
    st->opcodes[0x0D] = (opcode) {"ORA", 0, 0, 4, op_ORA, AM_ABSOLUTE};
    st->opcodes[0x1D] = (opcode) {"ORA", 0, x, -4, op_ORA, AM_ABSOLUTE};
    st->opcodes[0x19] = (opcode) {"ORA", 0, y, -4, op_ORA, AM_ABSOLUTE};
    st->opcodes[0x01] = (opcode) {"ORA", 0, 0, 6, op_ORA, AM_INDIRECT_X};
    st->opcodes[0x11] = (opcode) {"ORA", 0, 0, -5, op_ORA, AM_INDIRECT_Y};
    
    st->opcodes[0xC9] = (opcode) {"CMP", a, 0, 2, op_CMP, AM_IMMEDIATE};
    st->opcodes[0xC5] = (opcode) {"CMP", a, 0, 3, op_CMP, AM_ZP};
    st->opcodes[0xD5] = (opcode) {"CMP", a, x, 4, op_CMP, AM_ZP};
    st->opcodes[0xCD] = (opcode) {"CMP", a, 0, 4, op_CMP, AM_ABSOLUTE};
    st->opcodes[0xDD] = (opcode) {"CMP", a, x, -4, op_CMP, AM_ABSOLUTE};
    st->opcodes[0xD9] = (opcode) {"CMP", a, y, -4, op_CMP, AM_ABSOLUTE};
    st->opcodes[0xC1] = (opcode) {"CMP", a, 0, 6, op_CMP, AM_INDIRECT_X};
    st->opcodes[0xD1] = (opcode) {"CMP", a, 0, -5, op_CMP, AM_INDIRECT_Y};
    st->opcodes[0xE0] = (opcode) {"CPX", x, 0, 2, op_CMP, AM_IMMEDIATE};
    st->opcodes[0xE4] = (opcode) {"CPX", x, 0, 3, op_CMP, AM_ZP};
    st->opcodes[0xEC] = (opcode) {"CPX", x, 0, 4, op_CMP, AM_ABSOLUTE};
    st->opcodes[0xC0] = (opcode) {"CPY", y, 0, 2, op_CMP, AM_IMMEDIATE};
    st->opcodes[0xC4] = (opcode) {"CPY", y, 0, 3, op_CMP, AM_ZP};
    st->opcodes[0xCC] = (opcode) {"CPY", y, 0, 4, op_CMP, AM_ABSOLUTE};
    
    st->opcodes[0x24] = (opcode) {"BIT", 0, 0, 3, op_BIT, AM_ZP};
    st->opcodes[0x2C] = (opcode) {"BIT", 0, 0, 4, op_BIT, AM_ABSOLUTE};
    
    st->opcodes[0xE6] = (opcode) {"INC", 0, 0, 5, op_INC, AM_ZP};
    st->opcodes[0xF6] = (opcode) {"INC", 0, x, 6, op_INC, AM_ZP};
    st->opcodes[0xEE] = (opcode) {"INC", 0, 0, 6, op_INC, AM_ABSOLUTE};
    st->opcodes[0xFE] = (opcode) {"INC", 0, x, 7, op_INC, AM_ABSOLUTE};
    st->opcodes[0xE8] = (opcode) {"INX", x, 0, 2, op_IN, AM_IMPLIED};
    st->opcodes[0xC8] = (opcode) {"INY", y, 0, 2, op_IN, AM_IMPLIED};
    
    st->opcodes[0xC6] = (opcode) {"DEC", 0, 0, 5, op_DEC, AM_ZP};
    st->opcodes[0xD6] = (opcode) {"DEC", 0, x, 6, op_DEC, AM_ZP};
    st->opcodes[0xCE] = (opcode) {"DEC", 0, 0, 6, op_DEC, AM_ABSOLUTE};
    st->opcodes[0xDE] = (opcode) {"DEC", 0, x, 7, op_DEC, AM_ABSOLUTE};
    st->opcodes[0xCA] = (opcode) {"DEX", x, 0, 2, op_DE, AM_IMPLIED};
    st->opcodes[0x88] = (opcode) {"DEY", y, 0, 2, op_DE, AM_IMPLIED};
    
    st->opcodes[0x0A] = (opcode) {"ASL A", a, 0, 2, op_ASL, AM_IMPLIED};
    st->opcodes[0x06] = (opcode) {"ASL", 0, 0, 5, op_ASL, AM_ZP};
    st->opcodes[0x16] = (opcode) {"ASL", 0, x, 6, op_ASL, AM_ZP};
    st->opcodes[0x0E] = (opcode) {"ASL", 0, 0, 6, op_ASL, AM_ABSOLUTE};
    st->opcodes[0x1E] = (opcode) {"ASL", 0, x, 7, op_ASL, AM_ABSOLUTE};
    
    st->opcodes[0x0A] = (opcode) {"LSR A", a, 0, 2, op_LSR, AM_IMPLIED};
    st->opcodes[0x06] = (opcode) {"LSR", 0, 0, 5, op_LSR, AM_ZP};
    st->opcodes[0x16] = (opcode) {"LSR", 0, x, 6, op_LSR, AM_ZP};
    st->opcodes[0x0E] = (opcode) {"LSR", 0, 0, 6, op_LSR, AM_ABSOLUTE};
    st->opcodes[0x1E] = (opcode) {"LSR", 0, x, 7, op_LSR, AM_ABSOLUTE};
    
    st->opcodes[0x0A] = (opcode) {"ROL A", a, 0, 2, op_ROL, AM_IMPLIED};
    st->opcodes[0x06] = (opcode) {"ROL", 0, 0, 5, op_ROL, AM_ZP};
    st->opcodes[0x16] = (opcode) {"ROL", 0, x, 6, op_ROL, AM_ZP};
    st->opcodes[0x0E] = (opcode) {"ROL", 0, 0, 6, op_ROL, AM_ABSOLUTE};
    st->opcodes[0x1E] = (opcode) {"ROL", 0, x, 7, op_ROL, AM_ABSOLUTE};
    
    st->opcodes[0x0A] = (opcode) {"ROR A", a, 0, 2, op_ROR, AM_IMPLIED};
    st->opcodes[0x06] = (opcode) {"ROR", 0, 0, 5, op_ROR, AM_ZP};
    st->opcodes[0x16] = (opcode) {"ROR", 0, x, 6, op_ROR, AM_ZP};
    st->opcodes[0x0E] = (opcode) {"ROR", 0, 0, 6, op_ROR, AM_ABSOLUTE};
    st->opcodes[0x1E] = (opcode) {"ROR", 0, x, 7, op_ROR, AM_ABSOLUTE};
    
    st->opcodes[0x4C] = (opcode) {"JMP", 0, 0, 3, op_JMP, AM_ABSOLUTE};
    st->opcodes[0x6C] = (opcode) {"JMP", 0, 0, 5, op_JMP, AM_INDIRECT_WORD};
    st->opcodes[0x20] = (opcode) {"JSR", 0, 0, 6, op_JSR, AM_ABSOLUTE};
    st->opcodes[0x40] = (opcode) {"RTI", 0, 0, 6, op_RTI, AM_IMPLIED};
    st->opcodes[0x60] = (opcode) {"RTS", 0, 0, 6, op_RTS, AM_IMPLIED};
    
    st->opcodes[0x10] = (opcode) {"BPL", 0, 0, 2, op_BPL, AM_RELATIVE};
    st->opcodes[0x30] = (opcode) {"BMI", 0, 0, 2, op_BMI, AM_RELATIVE};
    st->opcodes[0x50] = (opcode) {"BVC", 0, 0, 2, op_BVC, AM_RELATIVE};
    st->opcodes[0x70] = (opcode) {"BVS", 0, 0, 2, op_BVS, AM_RELATIVE};
    st->opcodes[0x90] = (opcode) {"BCC", 0, 0, 2, op_BCC, AM_RELATIVE};
    st->opcodes[0xB0] = (opcode) {"BCS", 0, 0, 2, op_BCS, AM_RELATIVE};
    st->opcodes[0xD0] = (opcode) {"BNE", 0, 0, 2, op_BNE, AM_RELATIVE};
    st->opcodes[0xF0] = (opcode) {"BEQ", 0, 0, 2, op_BEQ, AM_RELATIVE};
    
    st->opcodes[0x00] = (opcode) {"BRK", 0, 0, 0, op_BRK, AM_IMPLIED};
    
    st->opcodes[0x18] = (opcode) {"CLC", 0, 0, 2, op_CLC, AM_IMPLIED};
    st->opcodes[0x58] = (opcode) {"CLI", 0, 0, 2, op_CLI, AM_IMPLIED};
    st->opcodes[0xD8] = (opcode) {"CLD", 0, 0, 2, op_CLD, AM_IMPLIED};
    st->opcodes[0xB8] = (opcode) {"CLV", 0, 0, 2, op_CLV, AM_IMPLIED};
    st->opcodes[0x38] = (opcode) {"SEC", 0, 0, 2, op_SEC, AM_IMPLIED};
    st->opcodes[0x78] = (opcode) {"SEI", 0, 0, 2, op_SEI, AM_IMPLIED};
    st->opcodes[0xF8] = (opcode) {"SED", 0, 0, 2, op_SED, AM_IMPLIED};

    st->opcodes[0xEA] = (opcode) {"NOP", 0, 0, 2, NULL, AM_IMPLIED};
}

int cpu_step(cpu_state *st, bool verbose) {
    // Fetch next instruction
    uint8_t inst = mm_read(st->mm, st->pc++);
    const opcode *op = &st->opcodes[inst];
    if (!op->name) {
        printf("Invalid opcode %d", inst);
        return -1;
    }
    
    // Fetch parameter, if any
    uint8_t zp_addr;
    uint16_t pre_indexing = 0;
    op_param param;
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
                printf(" %d", param.relative_addr);
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

int cpu_irq(cpu_state *st) {
    if (get_p_flag(st, P_I)) {
        return 0;
    }
    return interrupt(st, false, IVT_IRQ);
}

int cpu_nmi(cpu_state *st) {
    return interrupt(st, false, IVT_NMI);
}

int cpu_reset(cpu_state *st) {
    return interrupt(st, true, IVT_RESET);
}

void cpu_debug_print_state(cpu_state *st) {
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
