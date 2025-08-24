// floason (C) 2025
// Licensed under the MIT License.

// I'm not intending for this to be super accurate, considering 
// this is the first time I'm writing an 808x emulator and I don't
// really have strong EE knowledge.

// Flags: anything marked with "U" is undefined, however I'm currently unfamiliar
// with how undefined behaviour operates on flags.

// Future references:
// . MUL/DIV/IDIV will use transpiled Intel microcode (if I can understand it),
//   because the timings for those fluctuate. 8086s aren't fast anyway.

#include <assert.h>

#include "cpu8086.h"
#include "util.h"

static const unsigned mask_buffer[2]    = { 0xFF, 0xFFFF };
static const unsigned sign_bit[2]       = { 7, 15 };

// https://graphics.stanford.edu/~seander/bithacks.html#ParityLookupTable
// (modified for 1 = even, 0 = odd)
static const bool parity_table[256] = 
{
#   define P2(n) n, n^1, n^1, n
#   define P4(n) P2(n), P2(n^1), P2(n^1), P2(n)
#   define P6(n) P4(n), P4(n^1), P4(n^1), P4(n)
    P6(1), P6(0), P6(0), P6(1)
};

static inline bool calculate_parity(unsigned value, bool is_word)
{
    if (is_word)
        return parity_table[value & 0xFF] ^ parity_table[(value >> 8) & 0xFF];
    else
        return parity_table[value & 0xFF];
}

struct opcode;

static void op_aaa(struct opcode* op, struct cpu8086* cpu);
static void op_aas(struct opcode* op, struct cpu8086* cpu);
static void op_adc(struct opcode* op, struct cpu8086* cpu);
static void op_add(struct opcode* op, struct cpu8086* cpu);
static void op_and(struct opcode* op, struct cpu8086* cpu);
static void op_callfar(struct opcode* op, struct cpu8086* cpu);
static void op_cbw(struct opcode* op, struct cpu8086* cpu);
static void op_cmp(struct opcode* op, struct cpu8086* cpu);
static void op_cwd(struct opcode* op, struct cpu8086* cpu);
static void op_daa(struct opcode* op, struct cpu8086* cpu);
static void op_das(struct opcode* op, struct cpu8086* cpu);
static void op_dec(struct opcode* op, struct cpu8086* cpu);
static void op_imm(struct opcode* op, struct cpu8086* cpu);
static void op_inc(struct opcode* op, struct cpu8086* cpu);
static void op_ja(struct opcode* op, struct cpu8086* cpu);
static void op_jae(struct opcode* op, struct cpu8086* cpu);
static void op_jb(struct opcode* op, struct cpu8086* cpu);
static void op_jbe(struct opcode* op, struct cpu8086* cpu);
static void op_je(struct opcode* op, struct cpu8086* cpu);
static void op_jg(struct opcode* op, struct cpu8086* cpu);
static void op_jge(struct opcode* op, struct cpu8086* cpu);
static void op_jl(struct opcode* op, struct cpu8086* cpu);
static void op_jle(struct opcode* op, struct cpu8086* cpu);
static void op_jne(struct opcode* op, struct cpu8086* cpu);
static void op_jno(struct opcode* op, struct cpu8086* cpu);
static void op_jnp(struct opcode* op, struct cpu8086* cpu);
static void op_jns(struct opcode* op, struct cpu8086* cpu);
static void op_jo(struct opcode* op, struct cpu8086* cpu);
static void op_jp(struct opcode* op, struct cpu8086* cpu);
static void op_js(struct opcode* op, struct cpu8086* cpu);
static void op_lahf(struct opcode* op, struct cpu8086* cpu);
static void op_lea(struct opcode* op, struct cpu8086* cpu);
static void op_lds(struct opcode* op, struct cpu8086* cpu);
static void op_les(struct opcode* op, struct cpu8086* cpu);
static void op_mov(struct opcode* op, struct cpu8086* cpu);
static void op_or(struct opcode* op, struct cpu8086* cpu);
static void op_pop(struct opcode* op, struct cpu8086* cpu);
static void op_popf(struct opcode* op, struct cpu8086* cpu);
static void op_push(struct opcode* op, struct cpu8086* cpu);
static void op_pushf(struct opcode* op, struct cpu8086* cpu);
static void op_retnear(struct opcode* op, struct cpu8086* cpu);
static void op_retfar(struct opcode* op, struct cpu8086* cpu);
static void op_sahf(struct opcode* op, struct cpu8086* cpu);
static void op_sbb(struct opcode* op, struct cpu8086* cpu);
static void op_sub(struct opcode* op, struct cpu8086* cpu);
static void op_test(struct opcode* op, struct cpu8086* cpu);
static void op_wait(struct opcode* op, struct cpu8086* cpu);
static void op_xchg(struct opcode* op, struct cpu8086* cpu);
static void op_xor(struct opcode* op, struct cpu8086* cpu);

// This is different from enum location_type.
// Whereas location_type is resolved when the instruction being read
// has been decoded, enum opcode_location is used to dictate how to
// decode the instruction.
enum opcode_location
{
    // reg16
    LOC_AX,
    LOC_CX,
    LOC_DX,
    LOC_BX,
    LOC_SP,
    LOC_BP,
    LOC_SI,
    LOC_DI,
    LOC_ES,
    LOC_CS,
    LOC_SS,
    LOC_DS,

    // reg8
    LOC_AL,
    LOC_CL,
    LOC_DL,
    LOC_BL,
    LOC_AH,
    LOC_CH,
    LOC_DH,
    LOC_BH,

    // immed
    LOC_IMM,
    LOC_IMM8,   // unique solely to opcode 0x83 (group IMM)

    // ModRM
    LOC_RM,
    LOC_REG,
    LOC_SREG,

    // address
    LOC_ADDR,
    LOC_SEGOFF, // segment:offset, only used for CALL/JMP

    // string
    LOC_STRSRC,
    LOC_STRDST,

    LOC_NULL
};

struct opcode
{
    const char* name;
    enum opcode_location destination;
    enum opcode_location source;
    bool is_word;
    bool is_string;
    void (*func)(struct opcode* op, struct cpu8086* cpu);
};

// Root opcode table.
static struct opcode op_table[] = 
{
    // 0x00 to 0x0F
    { "ADD",    LOC_RM,     LOC_REG,    false,  false,  op_add },
    { "ADD",    LOC_RM,     LOC_REG,    true,   false,  op_add },
    { "ADD",    LOC_REG,    LOC_RM,     false,  false,  op_add },
    { "ADD",    LOC_REG,    LOC_RM,     true,   false,  op_add },
    { "ADD",    LOC_AL,     LOC_IMM,    false,  false,  op_add },
    { "ADD",    LOC_AX,     LOC_IMM,    true,   false,  op_add },
    { "PUSH",   LOC_ES,     LOC_NULL,   true,   false,  op_push },
    { "POP",    LOC_ES,     LOC_NULL,   true,   false,  op_pop },
    { "OR",     LOC_RM,     LOC_REG,    false,  false,  op_or },
    { "OR",     LOC_RM,     LOC_REG,    true,   false,  op_or },
    { "OR",     LOC_REG,    LOC_RM,     false,  false,  op_or },
    { "OR",     LOC_REG,    LOC_RM,     true,   false,  op_or },
    { "OR",     LOC_AL,     LOC_IMM,    false,  false,  op_or },
    { "OR",     LOC_AX,     LOC_IMM,    true,   false,  op_or },
    { "PUSH",   LOC_CS,     LOC_NULL,   true,   false,  op_push },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },         // Should be POP CS?
    
    // 0x10 to 0x1F
    { "ADC",    LOC_RM,     LOC_REG,    false,  false,  op_adc },
    { "ADC",    LOC_RM,     LOC_REG,    true,   false,  op_adc },
    { "ADC",    LOC_REG,    LOC_RM,     false,  false,  op_adc },
    { "ADC",    LOC_REG,    LOC_RM,     true,   false,  op_adc },
    { "ADC",    LOC_AL,     LOC_IMM,    false,  false,  op_adc },
    { "ADC",    LOC_AX,     LOC_IMM,    true,   false,  op_adc },
    { "PUSH",   LOC_SS,     LOC_NULL,   true,   false,  op_push },
    { "POP",    LOC_SS,     LOC_NULL,   true,   false,  op_pop },
    { "SBB",    LOC_RM,     LOC_REG,    false,  false,  op_sbb },
    { "SBB",    LOC_RM,     LOC_REG,    true,   false,  op_sbb },
    { "SBB",    LOC_REG,    LOC_RM,     false,  false,  op_sbb },
    { "SBB",    LOC_REG,    LOC_RM,     true,   false,  op_sbb },
    { "SBB",    LOC_AL,     LOC_IMM,    false,  false,  op_sbb },
    { "SBB",    LOC_AX,     LOC_IMM,    true,   false,  op_sbb },
    { "PUSH",   LOC_DS,     LOC_NULL,   true,   false,  op_push },
    { "POP",    LOC_DS,     LOC_NULL,   true,   false,  op_pop },

    // 0x20 to 0x2F
    { "AND",    LOC_RM,     LOC_REG,    false,  false,  op_and },
    { "AND",    LOC_RM,     LOC_REG,    true,   false,  op_and },
    { "AND",    LOC_REG,    LOC_RM,     false,  false,  op_and },
    { "AND",    LOC_REG,    LOC_RM,     true,   false,  op_and },
    { "AND",    LOC_AL,     LOC_IMM,    false,  false,  op_and },
    { "AND",    LOC_AX,     LOC_IMM,    true,   false,  op_and },
    { "ES:",    LOC_NULL,   LOC_NULL,   false,  false,  NULL },         // PREFIX ES:
    { "DAA",    LOC_NULL,   LOC_NULL,   false,  false,  op_daa },
    { "SUB",    LOC_RM,     LOC_REG,    false,  false,  op_sub },
    { "SUB",    LOC_RM,     LOC_REG,    true,   false,  op_sub },
    { "SUB",    LOC_REG,    LOC_RM,     false,  false,  op_sub },
    { "SUB",    LOC_REG,    LOC_RM,     true,   false,  op_sub },
    { "SUB",    LOC_AL,     LOC_IMM,    false,  false,  op_sub },
    { "SUB",    LOC_AX,     LOC_IMM,    true,   false,  op_sub },
    { "CS:",    LOC_NULL,   LOC_NULL,   false,  false,  NULL },         // PREFIX CS:
    { "DAS",    LOC_NULL,   LOC_NULL,   false,  false,  op_das },

    // 0x30 to 0x3F
    { "XOR",    LOC_RM,     LOC_REG,    false,  false,  op_xor },
    { "XOR",    LOC_RM,     LOC_REG,    true,   false,  op_xor },
    { "XOR",    LOC_REG,    LOC_RM,     false,  false,  op_xor },
    { "XOR",    LOC_REG,    LOC_RM,     true,   false,  op_xor },
    { "XOR",    LOC_AL,     LOC_IMM,    false,  false,  op_xor },
    { "XOR",    LOC_AX,     LOC_IMM,    true,   false,  op_xor },
    { "SS:",    LOC_NULL,   LOC_NULL,   false,  false,  NULL },         // PREFIX SS:
    { "AAA",    LOC_NULL,   LOC_NULL,   false,  false,  op_aaa },
    { "CMP",    LOC_RM,     LOC_REG,    false,  false,  op_cmp },
    { "CMP",    LOC_RM,     LOC_REG,    true,   false,  op_cmp },
    { "CMP",    LOC_REG,    LOC_RM,     false,  false,  op_cmp },
    { "CMP",    LOC_REG,    LOC_RM,     true,   false,  op_cmp },
    { "CMP",    LOC_AL,     LOC_IMM,    false,  false,  op_cmp },
    { "CMP",    LOC_AX,     LOC_IMM,    true,   false,  op_cmp },
    { "DS:",    LOC_NULL,   LOC_NULL,   false,  false,  NULL },         // PREFIX DS:
    { "AAS",    LOC_NULL,   LOC_NULL,   false,  false,  op_aas },

    // 0x40 to 0x4F
    { "INC",    LOC_AX,     LOC_NULL,   true,   false,  op_inc },
    { "INC",    LOC_CX,     LOC_NULL,   true,   false,  op_inc },
    { "INC",    LOC_DX,     LOC_NULL,   true,   false,  op_inc },
    { "INC",    LOC_BX,     LOC_NULL,   true,   false,  op_inc },
    { "INC",    LOC_SP,     LOC_NULL,   true,   false,  op_inc },
    { "INC",    LOC_BP,     LOC_NULL,   true,   false,  op_inc },
    { "INC",    LOC_SI,     LOC_NULL,   true,   false,  op_inc },
    { "INC",    LOC_DI,     LOC_NULL,   true,   false,  op_inc },
    { "DEC",    LOC_AX,     LOC_NULL,   true,   false,  op_dec },
    { "DEC",    LOC_CX,     LOC_NULL,   true,   false,  op_dec },
    { "DEC",    LOC_DX,     LOC_NULL,   true,   false,  op_dec },
    { "DEC",    LOC_BX,     LOC_NULL,   true,   false,  op_dec },
    { "DEC",    LOC_SP,     LOC_NULL,   true,   false,  op_dec },
    { "DEC",    LOC_BP,     LOC_NULL,   true,   false,  op_dec },
    { "DEC",    LOC_SI,     LOC_NULL,   true,   false,  op_dec },
    { "DEC",    LOC_DI,     LOC_NULL,   true,   false,  op_dec },

    // 0x50 to 0x5F
    { "PUSH",   LOC_AX,     LOC_NULL,   true,   false,  op_push },
    { "PUSH",   LOC_CX,     LOC_NULL,   true,   false,  op_push },
    { "PUSH",   LOC_DX,     LOC_NULL,   true,   false,  op_push },
    { "PUSH",   LOC_BX,     LOC_NULL,   true,   false,  op_push },
    { "PUSH",   LOC_SP,     LOC_NULL,   true,   false,  op_push },
    { "PUSH",   LOC_BP,     LOC_NULL,   true,   false,  op_push },
    { "PUSH",   LOC_SI,     LOC_NULL,   true,   false,  op_push },
    { "PUSH",   LOC_DI,     LOC_NULL,   true,   false,  op_push },
    { "POP",    LOC_AX,     LOC_NULL,   true,   false,  op_pop },
    { "POP",    LOC_CX,     LOC_NULL,   true,   false,  op_pop },
    { "POP",    LOC_DX,     LOC_NULL,   true,   false,  op_pop },
    { "POP",    LOC_BX,     LOC_NULL,   true,   false,  op_pop },
    { "POP",    LOC_SP,     LOC_NULL,   true,   false,  op_pop },
    { "POP",    LOC_BP,     LOC_NULL,   true,   false,  op_pop },
    { "POP",    LOC_SI,     LOC_NULL,   true,   false,  op_pop },
    { "POP",    LOC_DI,     LOC_NULL,   true,   false,  op_pop },

    // 0x60 to 0x6F
    // I believe this just mirrors 0x70 - 0x7F, but I'm not focusing
    // on illegal instructions for now.
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },

    // 0x70 to 0x7F
    { "JO",     LOC_NULL,   LOC_IMM,    false,  false,  op_jo },
    { "JNO",    LOC_NULL,   LOC_IMM,    false,  false,  op_jno },
    { "JB",     LOC_NULL,   LOC_IMM,    false,  false,  op_jb },
    { "JAE",    LOC_NULL,   LOC_IMM,    false,  false,  op_jae },
    { "JE",     LOC_NULL,   LOC_IMM,    false,  false,  op_je },
    { "JNE",    LOC_NULL,   LOC_IMM,    false,  false,  op_jne },
    { "JBE",    LOC_NULL,   LOC_IMM,    false,  false,  op_jbe },
    { "JA",     LOC_NULL,   LOC_IMM,    false,  false,  op_ja },
    { "JS",     LOC_NULL,   LOC_IMM,    false,  false,  op_js },
    { "JNS",    LOC_NULL,   LOC_IMM,    false,  false,  op_jns },
    { "JP",     LOC_NULL,   LOC_IMM,    false,  false,  op_jp },
    { "JNP",    LOC_NULL,   LOC_IMM,    false,  false,  op_jnp },
    { "JL",     LOC_NULL,   LOC_IMM,    false,  false,  op_jl },
    { "JGE",    LOC_NULL,   LOC_IMM,    false,  false,  op_jge },
    { "JLE",    LOC_NULL,   LOC_IMM,    false,  false,  op_jle },
    { "JG",     LOC_NULL,   LOC_IMM,    false,  false,  op_jg },

    // 0x80 to 0x8F
    { "IMM",    LOC_RM,     LOC_IMM,    false,  false,  op_imm },
    { "IMM",    LOC_RM,     LOC_IMM,    true,   false,  op_imm },
    { "IMM",    LOC_RM,     LOC_IMM,    false,  false,  op_imm },
    { "IMM",    LOC_RM,     LOC_IMM8,   true,   false,  op_imm },
    { "TEST",   LOC_REG,    LOC_RM,     false,  false,  op_test },
    { "TEST",   LOC_REG,    LOC_RM,     true,   false,  op_test },
    { "XCHG",   LOC_REG,    LOC_RM,     false,  false,  op_xchg },
    { "XCHG",   LOC_REG,    LOC_RM,     true,   false,  op_xchg },
    { "MOV",    LOC_RM,     LOC_REG,    false,  false,  op_mov },
    { "MOV",    LOC_RM,     LOC_REG,    true,   false,  op_mov },
    { "MOV",    LOC_REG,    LOC_RM,     false,  false,  op_mov },
    { "MOV",    LOC_REG,    LOC_RM,     true,   false,  op_mov },
    { "MOV",    LOC_RM,     LOC_SREG,   true,   false,  op_mov },
    { "LEA",    LOC_REG,    LOC_RM,     true,   false,  op_lea },
    { "MOV",    LOC_SREG,   LOC_RM,     true,   false,  op_mov },
    { "POP",    LOC_RM,     LOC_NULL,   true,   false,  op_pop },

    // 0x90 to 0x9F
    { "NOP",    LOC_AX,     LOC_AX,     true,   false,  op_xchg },      // Technically XCHG AX AX.
    { "XCHG",   LOC_CX,     LOC_AX,     true,   false,  op_xchg },
    { "XCHG",   LOC_DX,     LOC_AX,     true,   false,  op_xchg },
    { "XCHG",   LOC_BX,     LOC_AX,     true,   false,  op_xchg },
    { "XCHG",   LOC_SP,     LOC_AX,     true,   false,  op_xchg },
    { "XCHG",   LOC_BP,     LOC_AX,     true,   false,  op_xchg },
    { "XCHG",   LOC_SI,     LOC_AX,     true,   false,  op_xchg },
    { "XCHG",   LOC_DI,     LOC_AX,     true,   false,  op_xchg },
    { "CBW",    LOC_NULL,   LOC_NULL,   true,   false,  op_cbw },
    { "CWD",    LOC_NULL,   LOC_NULL,   true,   false,  op_cwd },
    { "CALL",   LOC_NULL,   LOC_SEGOFF, true,   false,  op_callfar },
    { "WAIT",   LOC_NULL,   LOC_NULL,   false,  false,  op_wait },
    { "PUSHF",  LOC_NULL,   LOC_NULL,   false,  false,  op_pushf },
    { "POPF",   LOC_NULL,   LOC_NULL,   false,  false,  op_popf },
    { "SAHF",   LOC_NULL,   LOC_NULL,   false,  false,  op_sahf },
    { "LAHF",   LOC_NULL,   LOC_NULL,   false,  false,  op_lahf },

    // 0xA0 to 0xAF
    { "MOV",    LOC_AL,     LOC_ADDR,   false,  false,  op_mov },
    { "MOV",    LOC_AX,     LOC_ADDR,   true,   false,  op_mov },
    { "MOV",    LOC_ADDR,   LOC_AL,     false,  false,  op_mov },
    { "MOV",    LOC_ADDR,   LOC_AX,     true,   false,  op_mov },
    { "MOVSB",  LOC_STRDST, LOC_STRSRC, false,  true,   op_mov },
    { "MOVSW",  LOC_STRDST, LOC_STRSRC, true,   true,   op_mov },
    { "CMPSB",  LOC_STRSRC, LOC_STRDST, false,  true,   op_cmp },
    { "CMPSW",  LOC_STRSRC, LOC_STRDST, true,   true,   op_cmp },
    { "TEST",   LOC_AL,     LOC_IMM,    false,  false,  op_test },
    { "TEST",   LOC_AX,     LOC_IMM,    true,   false,  op_test },
    { "STOSB",  LOC_STRDST, LOC_AL,     false,  true,   op_mov },
    { "STOSW",  LOC_STRDST, LOC_AX,     true,   true,   op_mov },
    { "LODSB",  LOC_AL,     LOC_STRSRC, false,  true,   op_mov },
    { "LODSW",  LOC_AX,     LOC_STRSRC, true,   true,   op_mov },
    { "SCASB",  LOC_AL,     LOC_STRDST, false,  true,   op_cmp },
    { "SCASB",  LOC_AL,     LOC_STRDST, true,   true,   op_cmp },

    // 0xB0 to 0xBF
    { "MOV",    LOC_AL,     LOC_IMM,    false,  false,  op_mov },
    { "MOV",    LOC_CL,     LOC_IMM,    false,  false,  op_mov },
    { "MOV",    LOC_DL,     LOC_IMM,    false,  false,  op_mov },
    { "MOV",    LOC_BL,     LOC_IMM,    false,  false,  op_mov },
    { "MOV",    LOC_AH,     LOC_IMM,    false,  false,  op_mov },
    { "MOV",    LOC_CH,     LOC_IMM,    false,  false,  op_mov },
    { "MOV",    LOC_DH,     LOC_IMM,    false,  false,  op_mov },
    { "MOV",    LOC_BH,     LOC_IMM,    false,  false,  op_mov },
    { "MOV",    LOC_AX,     LOC_IMM,    true,   false,  op_mov },
    { "MOV",    LOC_CX,     LOC_IMM,    true,   false,  op_mov },
    { "MOV",    LOC_DX,     LOC_IMM,    true,   false,  op_mov },
    { "MOV",    LOC_BX,     LOC_IMM,    true,   false,  op_mov },
    { "MOV",    LOC_SP,     LOC_IMM,    true,   false,  op_mov },
    { "MOV",    LOC_BP,     LOC_IMM,    true,   false,  op_mov },
    { "MOV",    LOC_SI,     LOC_IMM,    true,   false,  op_mov },
    { "MOV",    LOC_DI,     LOC_IMM,    true,   false,  op_mov },

    // 0xC0 to 0xCF
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },         // Not sure what this is.
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },         // Nor this.
    { "RET",    LOC_NULL,   LOC_IMM,    true,   false,  op_retnear },
    { "RET",    LOC_NULL,   LOC_NULL,   true,   false,  op_retnear },
    { "LES",    LOC_REG,    LOC_RM,     true,   false,  op_les },
    { "LDS",    LOC_REG,    LOC_RM,     true,   false,  op_lds },
    { "MOV",    LOC_RM,     LOC_IMM,    false,  false,  op_mov },
    { "MOV",    LOC_RM,     LOC_IMM,    true,   false,  op_mov },
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },         // Not sure what this is.
    { "ILLEG.", LOC_NULL,   LOC_NULL,   true,   false,  NULL },         // Nor this.
    { "RET",    LOC_NULL,   LOC_IMM,    true,   false,  op_retfar },
    { "RET",    LOC_NULL,   LOC_NULL,   true,   false,  op_retfar },
    
};

// IMM group opcode table.
// Since the locations and word types are already decoded, they are
// just placeholders. While this could just be a table of function
// pointers, each opcode name is still specified for debugging
// purposes. 
static struct opcode imm_table[] =
{
    // 0x00 to 0x07
    { "ADD",    LOC_NULL,   LOC_NULL,   false,  false,  op_add },
    { "OR",     LOC_NULL,   LOC_NULL,   false,  false,  op_or },
    { "ADC",    LOC_NULL,   LOC_NULL,   false,  false,  op_adc },
    { "SBB",    LOC_NULL,   LOC_NULL,   false,  false,  op_sbb },
    { "AND",    LOC_NULL,   LOC_NULL,   false,  false,  op_and },
    { "SUB",    LOC_NULL,   LOC_NULL,   false,  false,  op_sub },
    { "XOR",    LOC_NULL,   LOC_NULL,   false,  false,  op_xor },
    { "CMP",    LOC_NULL,   LOC_NULL,   false,  false,  op_cmp },
};

static inline uint8_t loc_read_byte(struct cpu8086* cpu, struct location* loc)
{
    if (loc->virtual)
        return bus_read_byte(cpu->bus, loc->address);
    else
        return *(uint8_t*)loc->address;
}

static inline uint16_t loc_read_word(struct cpu8086* cpu, struct location* loc)
{
    if (loc->virtual)
    {
        if (loc->address & 1)
            cpu->cycles += 4;
        return bus_read_short(cpu->bus, loc->address);
    }
    else
        return *(uint16_t*)loc->address;
}

static inline void loc_write_byte(struct cpu8086* cpu, struct location* loc, uint8_t data)
{
    if (loc->virtual)
        bus_write_byte(cpu->bus, loc->address, data);
    else
        *(uint8_t*)loc->address = data;
}

static inline void loc_write_word(struct cpu8086* cpu, struct location* loc, uint16_t data)
{
    if (loc->virtual)
    {
        if (loc->address & 1)
            cpu->cycles += 4;
        bus_write_short(cpu->bus, loc->address, data);
    }
    else
        *(uint16_t*)loc->address = data;
}

static inline uint16_t loc_read(struct cpu8086* cpu, struct location* loc)
{
    if (op_table[cpu->opcode_byte].is_word)
        return loc_read_word(cpu, loc);
    else
        return loc_read_byte(cpu, loc);
}

static inline void loc_write(struct cpu8086* cpu, struct location* loc, uint16_t data)
{
    if (op_table[cpu->opcode_byte].is_word)
        loc_write_word(cpu, loc, data);
    else
        loc_write_byte(cpu, loc, data);
}

static inline void cpu8086_push(struct cpu8086* cpu, uint16_t word)
{
    cpu->sp -= 2;
    bus_write_short(cpu->bus, (cpu->ss << 4) + cpu->sp, word);
}

static inline uint16_t cpu8086_pop(struct cpu8086* cpu)
{
    uint16_t word = bus_read_short(cpu->bus, (cpu->ss << 4) + cpu->sp);;
    cpu->sp += 2;
    return word;
}

static inline uint16_t* cpu8086_reg_word(struct cpu8086* cpu, unsigned reg)
{
    // This works because the registers are organised in the cpu8086 struct
    // in the same order as the reg section of the ModR/M byte.
    assert(reg < (1 << REGISTER_COUNT)); // This function is used or seg-reg too.
    return (uint16_t*)(&cpu->ax) + reg;
}

static inline uint8_t* cpu8086_reg_byte(struct cpu8086* cpu, unsigned reg)
{
    // This works because the registers are organised in the cpu8086 struct
    // in the same order as the reg section of the ModR/M byte.
    assert(reg < (1 << REG_FIELD));
    return (uint8_t*)(&cpu->ax) + (reg & 0b11) * 2 + (reg & 0b100);
}

static inline uint8_t cpu8086_prefetch_dequeue(struct cpu8086* cpu)
{
    uint8_t read = (cpu->q[cpu->q_r] >> (cpu->hl * 8)) & 0xFF;
    if (cpu->hl)
    {
        cpu->q_r = (cpu->q_r + 1) % 3;   
        cpu->mt = cpu->q_r == cpu->q_w;
    }
    cpu->hl = !cpu->hl;
    cpu->current_ip++;
    return read;
}

static inline void cpu8086_jump(struct cpu8086* cpu, uint16_t cs, uint16_t ip)
{
    // Clear the prefetch queue.
    cpu->hl = false;
    cpu->mt = false;
    cpu->q_r = cpu->q_w;
    if (cpu->biu_prefetch_cycles != 3)
        cpu->biu_prefetch_cycles += 4;
    
    // Set new CS:IP.
    cpu->cs = cs;
    cpu->ip = cpu->current_ip = ip;
}

static inline void loc_set(struct cpu8086* cpu, 
                           struct location* loc, 
                           enum opcode_location type)
{
    switch (type)
    {
        case LOC_AX:
        case LOC_CX:
        case LOC_DX:
        case LOC_BX:
        case LOC_SP:
        case LOC_BP:
        case LOC_SI:
        case LOC_DI:
        {
            loc->type = (type == LOC_AX)
                      ? DECODED_ACCUMULATOR
                      : DECODED_REGISTER;
            loc->address = (uintptr_t)cpu8086_reg_word(cpu, (unsigned)type);
            loc->virtual = false;
            break;
        }
        case LOC_ES:
        case LOC_CS:
        case LOC_SS:
        case LOC_DS:
        {
            loc->type = DECODED_SEGREG;
            loc->address = (uintptr_t)cpu8086_reg_word(cpu, (unsigned)type);
            loc->virtual = false;
            break;
        }
        case LOC_AL:
        case LOC_CL:
        case LOC_DL:
        case LOC_BL:
        case LOC_AH:
        case LOC_CH:
        case LOC_DH:
        case LOC_BH:
        {
            loc->type = (type == LOC_AL || type == LOC_AH) 
                      ? DECODED_ACCUMULATOR
                      : DECODED_REGISTER;
            loc->address = (uintptr_t)cpu8086_reg_byte(cpu, (unsigned)type - (unsigned)LOC_AL);
            loc->virtual = false;
            break;
        }
        case LOC_IMM:
        case LOC_IMM8:
        case LOC_RM:
        {
            loc->type = (cpu->modrm_byte.fields.mod != MOD_REG)
                      ? DECODED_MEMORY
                      : cpu->modrm_is_segreg
                      ? DECODED_SEGREG
                      : DECODED_REGISTER;
            loc->address = cpu->rm;
            loc->virtual = cpu->modrm_byte.fields.mod != MOD_REG;
            break;
        }
        case LOC_REG:
        case LOC_SREG:
        {
            loc->type = DECODED_REGISTER;
            loc->address = cpu->reg;
            loc->virtual = false;
            break;
        }
        case LOC_ADDR:
        {
            loc->type = DECODED_MEMORY;
            loc->address = cpu->immediate;
            loc->virtual = true;
            break;
        }
        case LOC_SEGOFF:
        {
            loc->type = DECODED_IMMEDIATE;
            loc->address = (uintptr_t)&cpu->immediate;
            loc->virtual = false;
            break;
        }
        case LOC_STRSRC:
        {
            unsigned prefix = DS;
            if (cpu->prefix_g2 != PREFIX_G2_NONE)
                prefix = ES + (cpu->prefix_g2 - PREFIX_G2_ES) / 8;
            loc->type = DECODED_STRING;
            loc->address = ((*cpu8086_reg_word(cpu, prefix) << 4) + cpu->si) & 0xFFFFF;
            loc->virtual = true;
        }
        case LOC_STRDST:
        {
            loc->type = DECODED_STRING;
            loc->address = (cpu->es << 4) + cpu->di;
            loc->virtual = true;
        }
        case LOC_NULL:
        {
            loc->type = DECODED_NULL;
            break;
        }
    }
}

static inline bool cpu8086_getflag(struct cpu8086* cpu, unsigned flag)
{
    return !!(cpu->flags & flag);
}

static inline void cpu8086_setflag(struct cpu8086* cpu, unsigned flag, bool toggle)
{
    if (toggle)
        cpu->flags |= flag;
    else
        cpu->flags &= ~flag;
}

static inline void cpu8086_setpzs_flags(struct cpu8086* cpu, uint16_t result, bool is_word)
{
    cpu8086_setflag(cpu, FLAG_PARITY, calculate_parity(result, is_word));
    cpu8086_setflag(cpu, FLAG_ZERO, (result & mask_buffer[is_word]) == 0);
    cpu8086_setflag(cpu, FLAG_SIGN, (result >> sign_bit[is_word]) & 1);
}

static inline void cpu8086_reset_execution_regs(struct cpu8086* cpu)
{
    cpu->repeat = false;
    cpu->prefix_g1 = PREFIX_G1_NONE;
    cpu->prefix_g2 = PREFIX_G1_NONE;
    cpu->opcode_byte = OPCODE_NONE;
    cpu->disp8_byte = DISP8_NONE;
    cpu->disp16_byte = DISP16_NONE;
    cpu->imm8_byte = IMM8_NONE;
    cpu->imm16_byte = IMM16_NONE;
    cpu->lo_segment = LO_SEGMENT_NONE;
    cpu->hi_segment = HI_SEGMENT_NONE;
    cpu->stage = CPU8086_READY;
    cpu->modrm_byte.value = MODRM_NONE;
}

// AAA: ascii adjust for addition
// https://c9x.me/x86/html/file_module_x86_id_1.html
static void op_aaa(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t old_al = cpu->al;
    uint16_t added = 0;
    if ((cpu->al & 0xF) > 9 || cpu8086_getflag(cpu, FLAG_AUXILIARY))
    {
        cpu->ah += 1;
        cpu->al += added = 6;
        cpu8086_setflag(cpu, FLAG_AUXILIARY, true);
        cpu8086_setflag(cpu, FLAG_CARRY, true);
    }
    else
    {
        cpu8086_setflag(cpu, FLAG_AUXILIARY, false);
        cpu8086_setflag(cpu, FLAG_CARRY, false);
    }
    cpu->al &= 0xF;

    cpu8086_setpzs_flags(cpu, cpu->al, false); // U
    cpu8086_setflag(cpu, FLAG_OVERFLOW, (cpu->al ^ old_al) & (cpu->al ^ added) & 0x80); // U

    cpu->cycles += 4;
}

// AAA: ascii adjust for subtraction
// https://c9x.me/x86/html/file_module_x86_id_1.html
static void op_aas(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t old_al = cpu->al;
    uint16_t added = 0;
    if ((cpu->al & 0xF) > 9 || cpu8086_getflag(cpu, FLAG_AUXILIARY))
    {
        cpu->ah -= 1;
        cpu->al += added = (~6 + 1);
        cpu8086_setflag(cpu, FLAG_AUXILIARY, true);
        cpu8086_setflag(cpu, FLAG_CARRY, true);
    }
    else
    {
        cpu8086_setflag(cpu, FLAG_AUXILIARY, false);
        cpu8086_setflag(cpu, FLAG_CARRY, false);
    }
    cpu->al &= 0xF;

    cpu8086_setpzs_flags(cpu, cpu->al, false); // U
    cpu8086_setflag(cpu, FLAG_OVERFLOW, (cpu->al ^ old_al) & (cpu->al ^ added) & 0x80); // U

    cpu->cycles += 4;
}

// ADC: add two operands + the carry flag
static void op_adc(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t dest = loc_read(cpu, &cpu->destination);
    uint16_t src = loc_read(cpu, &cpu->source) + cpu8086_getflag(cpu, FLAG_CARRY);
    unsigned result = dest + src;

    loc_write(cpu, &cpu->destination, result);
    cpu8086_setpzs_flags(cpu, result, op->is_word);
    cpu8086_setflag(cpu, FLAG_CARRY, result > mask_buffer[op->is_word]);
    cpu8086_setflag(cpu, FLAG_AUXILIARY, (dest & 0xF) + (src & 0xF) > 0xF);
    cpu8086_setflag(cpu, FLAG_OVERFLOW, 
        (result ^ dest) & (result ^ src) & (1 << sign_bit[op->is_word]));
    
    // This looks ridiculous, but switch tables are faster than constantly doing
    // if-else if-else if-else if, etc.
    switch ((cpu->destination.type << 3) | cpu->source.type)
    {
        case (DECODED_REGISTER << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 3;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_MEMORY:
        {
            cpu->cycles += 9;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 16;
            break;
        }
        case (DECODED_ACCUMULATOR << 3) | DECODED_IMMEDIATE:
        case (DECODED_REGISTER << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 4;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 17;
            break;
        }
        default:
            assert(false);
    }
}

// ADD: add two operands
static void op_add(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t dest = loc_read(cpu, &cpu->destination);
    uint16_t src = loc_read(cpu, &cpu->source);
    unsigned result = dest + src;
    loc_write(cpu, &cpu->destination, result);
    
    cpu8086_setpzs_flags(cpu, result, op->is_word);
    cpu8086_setflag(cpu, FLAG_CARRY, result > mask_buffer[op->is_word]);
    cpu8086_setflag(cpu, FLAG_AUXILIARY, (dest & 0xF) + (src & 0xF) > 0xF);
    cpu8086_setflag(cpu, FLAG_OVERFLOW, 
        (result ^ dest) & (result ^ src) & (1 << sign_bit[op->is_word]));
    
    switch ((cpu->destination.type << 3) | cpu->source.type)
    {
        case (DECODED_REGISTER << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 3;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_MEMORY:
        {
            cpu->cycles += 9;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 16;
            break;
        }
        case (DECODED_ACCUMULATOR << 3) | DECODED_IMMEDIATE:
        case (DECODED_REGISTER << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 4;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 17;
            break;
        }
        default:
            assert(false);
    }
}

// AND: bitwise and two operands
static void op_and(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t dest = loc_read(cpu, &cpu->destination);
    uint16_t src = loc_read(cpu, &cpu->source);
    uint16_t result = dest & src;
    loc_write(cpu, &cpu->destination, result);

    cpu8086_setpzs_flags(cpu, result, op->is_word);
    cpu8086_setflag(cpu, FLAG_CARRY, false);
    cpu8086_setflag(cpu, FLAG_AUXILIARY, false); // U
    cpu8086_setflag(cpu, FLAG_OVERFLOW, false);

    switch ((cpu->destination.type << 3) | cpu->source.type)
    {
        case (DECODED_REGISTER << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 3;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_MEMORY:
        {
            cpu->cycles += 9;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 16;
            break;
        }
        case (DECODED_ACCUMULATOR << 3) | DECODED_IMMEDIATE:
        case (DECODED_REGISTER << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 4;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 17;
            break;
        }
        default:
            assert(false);
    }
}

// CALL (far): call a procedure by setting both CS:IP.
static void op_callfar(struct opcode* op, struct cpu8086* cpu)
{
    cpu8086_push(cpu, cpu->cs);
    cpu8086_push(cpu, cpu->current_ip);

    uint16_t ip = loc_read(cpu, &cpu->source);
    cpu->source.address += 2;
    uint16_t cs = loc_read(cpu, &cpu->source);
    cpu8086_jump(cpu, cs, ip);

    // TODO: expand
    switch (cpu->source.type)
    {
        case DECODED_IMMEDIATE:
        {
            cpu->cycles += 28;
            break;
        }
        default:
            assert(false);
    }
}

// CBW: convert byte to word by sign-extending AL to AX
static void op_cbw(struct opcode* op, struct cpu8086* cpu)
{
    cpu->ah = 0xFF * ((cpu->al >> 7) & 1);
    cpu->cycles += 2;
}

// CMP: subtract src from dest without storing, but still set flags
static void op_cmp(struct opcode* op, struct cpu8086* cpu)
{
    // It's easier to just use two's complement here.
    uint16_t dest = loc_read(cpu, &cpu->destination);
    uint16_t src = ~loc_read(cpu, &cpu->source) + 1;
    unsigned result = dest + src;
    
    cpu8086_setpzs_flags(cpu, result, op->is_word);
    cpu8086_setflag(cpu, FLAG_CARRY, result > mask_buffer[op->is_word]);
    cpu8086_setflag(cpu, FLAG_AUXILIARY, (dest & 0xF) < ((~src + 1) & 0xF));
    cpu8086_setflag(cpu, FLAG_OVERFLOW, 
        (result ^ dest) & (result ^ src) & (1 << sign_bit[op->is_word]));
    
    switch ((cpu->destination.type << 3) | cpu->source.type)
    {
        case (DECODED_REGISTER << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 3;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_MEMORY:
        {
            cpu->cycles += 9;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 9;
            break;
        }
        case (DECODED_ACCUMULATOR << 3) | DECODED_IMMEDIATE:
        case (DECODED_REGISTER << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 4;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 10;
            break;
        }

        // CMPS
        case (DECODED_STRING << 3) | DECODED_STRING:
        {
            cpu->cycles += 22;
            break;
        }

        // SCAS
        case (DECODED_ACCUMULATOR << 3) | DECODED_STRING:
        {
            cpu->cycles += 15;
        }

        default:
            assert(false);
    }
}

// CWD: convert word to doubleword by sign-extending AX to DX:AX
static void op_cwd(struct opcode* op, struct cpu8086* cpu)
{
    cpu->dx = 0xFFFF * ((cpu->ax >> 15) & 1);
    cpu->cycles += 5;
}

// DAA: "decimal adjust for addition"
// https://www.righto.com/2023/01/understanding-x86s-decimal-adjust-after.html
static void op_daa(struct opcode* op, struct cpu8086* cpu)
{
    uint8_t old_al = cpu->al;
    uint8_t added = 0;
    bool old_af = cpu8086_getflag(cpu, FLAG_AUXILIARY);

    if ((cpu->al & 0xF) > 9 || old_af)
    {
        added += 6;
        cpu8086_setflag(cpu, FLAG_AUXILIARY, true);
    }
    else
        cpu8086_setflag(cpu, FLAG_AUXILIARY, false);

    // According to GloriousCow, if AF (auxiliary) is set on the 8088, the value 
    // used to compare the initial value of AL against is actually 0x9F, not 0x99.
    // https://www.righto.com/2023/01/understanding-x86s-decimal-adjust-after.html?showComment=1677257126254#c6550878741725342730
    if (old_al > 0x99 + (old_af ? 6 : 0) || cpu8086_getflag(cpu, FLAG_CARRY))
    {
        added += 0x60;
        cpu8086_setflag(cpu, FLAG_CARRY, true);
    }
    else
        cpu8086_setflag(cpu, FLAG_CARRY, false);

    cpu->al += added;

    cpu8086_setpzs_flags(cpu, cpu->al, false);
    cpu8086_setflag(cpu, FLAG_OVERFLOW, (cpu->al ^ old_al) & (cpu->al ^ added) & 0x80); // U

    cpu->cycles += 4;
}

// DAS: "decimal adjust for subtraction"
// https://c9x.me/x86/html/file_module_x86_id_70.html
static void op_das(struct opcode* op, struct cpu8086* cpu)
{
    uint8_t old_al = cpu->al;
    uint8_t added = 0;
    bool old_af = cpu8086_getflag(cpu, FLAG_AUXILIARY);
    
    if ((cpu->al & 0xF) > 9 || old_af)
    {
        added += 6;
        cpu8086_setflag(cpu, FLAG_AUXILIARY, true);
    }
    else
        cpu8086_setflag(cpu, FLAG_AUXILIARY, false);

    if (cpu->al > 0x99 + (old_af ? 6 : 0) || cpu8086_getflag(cpu, FLAG_CARRY))
    {
        added += 0x60;
        cpu8086_setflag(cpu, FLAG_CARRY, true);
    }
    else
        cpu8086_setflag(cpu, FLAG_CARRY, false);

    added = ~added + 1; // This is subtraction, so we exploit two's complement.
    cpu->al += added;

    cpu8086_setpzs_flags(cpu, cpu->al, false);
    cpu8086_setflag(cpu, FLAG_OVERFLOW, (cpu->al ^ old_al) & (cpu->al ^ added) & 0x80); // U

    cpu->cycles += 4;
}

// DEC: decrement by 1
static void op_dec(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t dest = loc_read(cpu, &cpu->destination);
    unsigned result = dest - 1;
    loc_write(cpu, &cpu->destination, result);
    
    cpu8086_setpzs_flags(cpu, result, op->is_word);
    cpu8086_setflag(cpu, FLAG_AUXILIARY, (dest & 0xF) < 1);
    cpu8086_setflag(cpu, FLAG_OVERFLOW, 
        (result ^ dest) & (result ^ 0xFFFF) & (1 << sign_bit[op->is_word]));

    switch (cpu->destination.type)
    {
        case DECODED_ACCUMULATOR:
        case DECODED_REGISTER:
        {
            cpu->cycles += (op->is_word ? 2 : 3);
            break;
        }
        case DECODED_MEMORY:
        {
            cpu->cycles += 15;
            break;
        }
        default:
            assert(false);
    }
}

// Group IMM:
// - ADD
// - OR
// - ADC
// - SBB
// - AND
// - SUB
// - XOR
// - CMP
static void op_imm(struct opcode* op, struct cpu8086* cpu)
{
    imm_table[cpu->modrm_byte.fields.reg].func(op, cpu);
}

// INC: increment by 1
static void op_inc(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t dest = loc_read(cpu, &cpu->destination);
    unsigned result = dest + 1;
    loc_write(cpu, &cpu->destination, result);
    
    cpu8086_setpzs_flags(cpu, result, op->is_word);
    cpu8086_setflag(cpu, FLAG_AUXILIARY, (dest & 0xF) + 1 > 0xF);
    cpu8086_setflag(cpu, FLAG_OVERFLOW, 
        (result ^ dest) & (result ^ 1) & (1 << sign_bit[op->is_word]));

    switch (cpu->destination.type)
    {
        case DECODED_ACCUMULATOR:
        case DECODED_REGISTER:
        {
            cpu->cycles += (op->is_word ? 2 : 3);
            break;
        }
        case DECODED_MEMORY:
        {
            cpu->cycles += 15;
            break;
        }
        default:
            assert(false);
    }
}

// JA: jump if above
static void op_ja(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (!cpu8086_getflag(cpu, FLAG_CARRY) && !cpu8086_getflag(cpu, FLAG_ZERO))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JAE: jump if above or equal
static void op_jae(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (!cpu8086_getflag(cpu, FLAG_CARRY))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JB: jump if below
static void op_jb(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (cpu8086_getflag(cpu, FLAG_CARRY))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JBE: jump if below or equal to
static void op_jbe(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (cpu8086_getflag(cpu, FLAG_CARRY) || cpu8086_getflag(cpu, FLAG_ZERO))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JE: jump if equal
static void op_je(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (cpu8086_getflag(cpu, FLAG_ZERO))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JG: jump if greater
static void op_jg(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (cpu8086_getflag(cpu, FLAG_SIGN) == cpu8086_getflag(cpu, FLAG_OVERFLOW)
        && !cpu8086_getflag(cpu, FLAG_ZERO))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JGE: jump if greater or equal
static void op_jge(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (cpu8086_getflag(cpu, FLAG_SIGN) == cpu8086_getflag(cpu, FLAG_OVERFLOW))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JL: jump if less
static void op_jl(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (cpu8086_getflag(cpu, FLAG_SIGN) != cpu8086_getflag(cpu, FLAG_OVERFLOW))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JLE: jump if less or equal
static void op_jle(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (cpu8086_getflag(cpu, FLAG_SIGN) != cpu8086_getflag(cpu, FLAG_OVERFLOW)
        || cpu8086_getflag(cpu, FLAG_ZERO))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JNE: jump if not equal
static void op_jne(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (!cpu8086_getflag(cpu, FLAG_ZERO))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JNO: jump if not overflow
static void op_jno(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (!cpu8086_getflag(cpu, FLAG_OVERFLOW))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JNS: jump if not parity
static void op_jnp(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (!cpu8086_getflag(cpu, FLAG_PARITY))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JNS: jump if not sign
static void op_jns(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (!cpu8086_getflag(cpu, FLAG_SIGN))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JO: jump if overflow
static void op_jo(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (cpu8086_getflag(cpu, FLAG_OVERFLOW))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JP: jump if parity
static void op_jp(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (cpu8086_getflag(cpu, FLAG_PARITY))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// JS: jump if sign
static void op_js(struct opcode* op, struct cpu8086* cpu)
{
    int8_t offset = loc_read(cpu, &cpu->source);
    if (cpu8086_getflag(cpu, FLAG_SIGN))
    {
        cpu8086_jump(cpu, cpu->cs, cpu->current_ip + offset);
        cpu->cycles += 12;
    }

    cpu->cycles += 4;
}

// LAHF: load AH from FLAGS
static void op_lahf(struct opcode* op, struct cpu8086* cpu)
{
    cpu->ah = cpu->flags;
    cpu->cycles += 4;
}

// LEA: load effective address into register destination
static void op_lea(struct opcode* op, struct cpu8086* cpu)
{
    assert(cpu->source.virtual); // TODO: how does this actually work?

    loc_write(cpu, &cpu->destination, cpu->source.address);
    cpu->cycles += 2;
}

// LDS: load [mem32] into reg16 and [mem32 + 2] into ES
static void op_lds(struct opcode* op, struct cpu8086* cpu)
{
    assert(cpu->source.virtual); // TODO: how does this actually work?

    uint16_t offset = loc_read(cpu, &cpu->source);
    cpu->source.address += 2;
    uint16_t segment = loc_read(cpu, &cpu->source);

    loc_write(cpu, &cpu->destination, offset);
    cpu->es = segment;

    cpu->cycles += 16;
}

// LES: load [mem32] into reg16 and [mem32 + 2] into ES
static void op_les(struct opcode* op, struct cpu8086* cpu)
{
    assert(cpu->source.virtual); // TODO: how does this actually work?

    uint16_t offset = loc_read(cpu, &cpu->source);
    cpu->source.address += 2;
    uint16_t segment = loc_read(cpu, &cpu->source);

    loc_write(cpu, &cpu->destination, offset);
    cpu->es = segment;

    cpu->cycles += 16;
}

// MOV: copy from source to destination
static void op_mov(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t src = loc_read(cpu, &cpu->source);
    loc_write(cpu, &cpu->destination, src);

    switch ((cpu->destination.type << 3) | cpu->source.type)
    {
        case (DECODED_MEMORY << 3) | DECODED_ACCUMULATOR:
        case (DECODED_ACCUMULATOR << 3) | DECODED_MEMORY:
        {
            cpu->cycles += 10;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_REGISTER:
        case (DECODED_SEGREG << 3) | DECODED_REGISTER:
        case (DECODED_REGISTER << 3) | DECODED_SEGREG:
        {
            cpu->cycles += 2;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_MEMORY:
        case (DECODED_SEGREG << 3) | DECODED_MEMORY:
        {
            cpu->cycles += 8;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_REGISTER:
        case (DECODED_MEMORY << 3) | DECODED_SEGREG:
        {
            cpu->cycles += 9;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_IMMEDIATE:
        case (DECODED_ACCUMULATOR << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 4;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 10;
            break;
        }

        // MOVS
        case (DECODED_STRING << 3) | DECODED_STRING:
        {
            cpu->cycles += cpu->repeat ? 17 : 18;
            break;
        }
        
        // STOS
        case (DECODED_STRING << 3) | DECODED_ACCUMULATOR:
        {
            cpu->cycles += cpu->repeat ? 10 : 11;
            break;
        }

        // LODS
        case (DECODED_ACCUMULATOR << 3) | DECODED_STRING:
        {
            cpu->cycles += cpu->repeat ? 13 : 12;
            break;
        }

        default:
            assert(false);
    }
}

// OR: bitwise or two operands
static void op_or(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t dest = loc_read(cpu, &cpu->destination);
    uint16_t src = loc_read(cpu, &cpu->source);
    uint16_t result = dest | src;
    loc_write(cpu, &cpu->destination, result);

    cpu8086_setpzs_flags(cpu, result, op->is_word);
    cpu8086_setflag(cpu, FLAG_CARRY, false);
    cpu8086_setflag(cpu, FLAG_AUXILIARY, false); // U
    cpu8086_setflag(cpu, FLAG_OVERFLOW, false);

    switch ((cpu->destination.type << 3) | cpu->source.type)
    {
        case (DECODED_REGISTER << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 3;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_MEMORY:
        {
            cpu->cycles += 9;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 16;
            break;
        }
        case (DECODED_ACCUMULATOR << 3) | DECODED_IMMEDIATE:
        case (DECODED_REGISTER << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 4;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 17;
            break;
        }
        default:
            assert(false);
    }
}

// POP: pop a word from the stack into a location
static void op_pop(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t result = cpu8086_pop(cpu);
    loc_write(cpu, &cpu->destination, result);

    switch (cpu->destination.type)
    {
        case DECODED_ACCUMULATOR:
        case DECODED_REGISTER:
        case DECODED_SEGREG:
        {
            cpu->cycles += 8;
            break;
        }
        case DECODED_MEMORY:
        {
            cpu->cycles += 17;
            break;
        }
        default:
            assert(false);
    }
}

// POPF: pop FLGAS off the stack
static void op_popf(struct opcode* op, struct cpu8086* cpu)
{
    cpu->flags = cpu8086_pop(cpu);
    cpu->cycles += 8;
}

// PUSH: push a word from a location onto the stack
static void op_push(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t dest = loc_read(cpu, &cpu->destination);
    cpu8086_push(cpu, dest);

    switch (cpu->destination.type)
    {
        case DECODED_ACCUMULATOR:
        case DECODED_REGISTER:
        {
            cpu->cycles += 11;
            break;
        }
        case DECODED_SEGREG:
        {
            cpu->cycles += 10;
            break;
        }
        case DECODED_MEMORY:
        {
            cpu->cycles += 16;
            break;
        }
        default:
            assert(false);
    }
}

// PUHSF: push FLAGS onto the stack
static void op_pushf(struct opcode* op, struct cpu8086* cpu)
{
    cpu8086_push(cpu, cpu->flags);
    cpu->cycles += 10;
}

// RET (near): pop the IP off the stack and release parameters off the stack
// if the invoked procedure uses the stdcall calling convention.
static void op_retnear(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t ip = cpu8086_pop(cpu);
    cpu8086_jump(cpu, cpu->cs, ip);

    switch (cpu->source.type)
    {
        case DECODED_NULL:
        {
            cpu->cycles += 8;
            break;
        }
        case DECODED_IMMEDIATE:
        {
            cpu->cycles += 12;
            cpu->sp += loc_read(cpu, &cpu->source);
            break;
        }
        default:
            assert(false);
    }
}

// RET (far): pop CS:IP off the stack and release parameters off the stack
// if the invoked procedure uses the stdcall calling convention.
static void op_retfar(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t ip = cpu8086_pop(cpu);
    uint16_t cs = cpu8086_pop(cpu);
    cpu8086_jump(cpu, cs, ip);

    switch (cpu->source.type)
    {
        case DECODED_NULL:
        {
            cpu->cycles += 18;
            break;
        }
        case DECODED_IMMEDIATE:
        {
            cpu->cycles += 17;
            cpu->sp += loc_read(cpu, &cpu->source);
            break;
        }
        default:
            assert(false);
    }
}

// SAHF: store AH into FLAGS
static void op_sahf(struct opcode* op, struct cpu8086* cpu)
{
    cpu8086_setflag(cpu, FLAG_CARRY, cpu->ah & FLAG_CARRY);
    cpu8086_setflag(cpu, FLAG_PARITY, cpu->ah & FLAG_PARITY);
    cpu8086_setflag(cpu, FLAG_AUXILIARY, cpu->ah & FLAG_AUXILIARY);
    cpu8086_setflag(cpu, FLAG_ZERO, cpu->ah & FLAG_ZERO);
    cpu8086_setflag(cpu, FLAG_SIGN, cpu->ah & FLAG_SIGN);
    cpu->cycles += 4;
}

// SBB: subtract src from dest, also subtract the carry flag
static void op_sbb(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t dest = loc_read(cpu, &cpu->destination);
    uint16_t src = ~(loc_read(cpu, &cpu->source) + cpu8086_getflag(cpu, FLAG_CARRY)) + 1;
    unsigned result = dest + src;
    loc_write(cpu, &cpu->destination, result);
    
    cpu8086_setpzs_flags(cpu, result, op->is_word);
    cpu8086_setflag(cpu, FLAG_CARRY, result > mask_buffer[op->is_word]);
    cpu8086_setflag(cpu, FLAG_AUXILIARY, (dest & 0xF) < ((~src + 1) & 0xF));
    cpu8086_setflag(cpu, FLAG_OVERFLOW, 
        (result ^ dest) & (result ^ src) & (1 << sign_bit[op->is_word]));
    
    switch ((cpu->destination.type << 3) | cpu->source.type)
    {
        case (DECODED_REGISTER << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 3;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_MEMORY:
        {
            cpu->cycles += 9;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 16;
            break;
        }
        case (DECODED_ACCUMULATOR << 3) | DECODED_IMMEDIATE:
        case (DECODED_REGISTER << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 4;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 17;
            break;
        }
        default:
            assert(false);
    }
}

// SUB: subtract src from dest
static void op_sub(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t dest = loc_read(cpu, &cpu->destination);
    uint16_t src = ~loc_read(cpu, &cpu->source) + 1;
    unsigned result = dest + src;
    loc_write(cpu, &cpu->destination, result);
    
    cpu8086_setpzs_flags(cpu, result, op->is_word);
    cpu8086_setflag(cpu, FLAG_CARRY, result > mask_buffer[op->is_word]);
    cpu8086_setflag(cpu, FLAG_AUXILIARY, (dest & 0xF) < ((~src + 1) & 0xF));
    cpu8086_setflag(cpu, FLAG_OVERFLOW, 
        (result ^ dest) & (result ^ src) & (1 << sign_bit[op->is_word]));
    
    switch ((cpu->destination.type << 3) | cpu->source.type)
    {
        case (DECODED_REGISTER << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 3;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_MEMORY:
        {
            cpu->cycles += 9;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 16;
            break;
        }
        case (DECODED_ACCUMULATOR << 3) | DECODED_IMMEDIATE:
        case (DECODED_REGISTER << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 4;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 17;
            break;
        }
        default:
            assert(false);
    }
}

// TEST: bitwise and two operands without setting destination
static void op_test(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t dest = loc_read(cpu, &cpu->destination);
    uint16_t src = loc_read(cpu, &cpu->source);
    uint16_t result = dest & src;

    cpu8086_setpzs_flags(cpu, result, op->is_word);
    cpu8086_setflag(cpu, FLAG_CARRY, false);
    cpu8086_setflag(cpu, FLAG_AUXILIARY, false); // U
    cpu8086_setflag(cpu, FLAG_OVERFLOW, false);

    switch ((cpu->destination.type << 3) | cpu->source.type)
    {
        case (DECODED_REGISTER << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 3;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_MEMORY:
        {
            cpu->cycles += 9;
            break;
        }
        case (DECODED_ACCUMULATOR << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 4;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 5;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 11;
            break;
        }
        default:
            assert(false);
    }
}

// WAIT: pause execution while TEST is held high
static void op_wait(struct opcode* op, struct cpu8086* cpu)
{
    // Handled in cpu8086_clock().
    cpu->cycles += 3;
}

// XCHG: exchange between destination/source
static void op_xchg(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t dest = loc_read(cpu, &cpu->destination);
    uint16_t src = loc_read(cpu, &cpu->source);
    loc_write(cpu, &cpu->destination, src);
    loc_write(cpu, &cpu->source, dest);

    switch ((cpu->destination.type << 3) | cpu->source.type)
    {
        case (DECODED_ACCUMULATOR << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 3;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 4;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 17;
            break;
        }
        default:
            assert(false);
    }
}

// XOR: bitwise xor two operands
static void op_xor(struct opcode* op, struct cpu8086* cpu)
{
    uint16_t dest = loc_read(cpu, &cpu->destination);
    uint16_t src = loc_read(cpu, &cpu->source);
    uint16_t result = dest ^ src;
    loc_write(cpu, &cpu->destination, result);

    cpu8086_setpzs_flags(cpu, result, op->is_word);
    cpu8086_setflag(cpu, FLAG_CARRY, false);
    cpu8086_setflag(cpu, FLAG_AUXILIARY, false); // U
    cpu8086_setflag(cpu, FLAG_OVERFLOW, false);

    switch ((cpu->destination.type << 3) | cpu->source.type)
    {
        case (DECODED_REGISTER << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 3;
            break;
        }
        case (DECODED_REGISTER << 3) | DECODED_MEMORY:
        {
            cpu->cycles += 9;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_REGISTER:
        {
            cpu->cycles += 16;
            break;
        }
        case (DECODED_ACCUMULATOR << 3) | DECODED_IMMEDIATE:
        case (DECODED_REGISTER << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 4;
            break;
        }
        case (DECODED_MEMORY << 3) | DECODED_IMMEDIATE:
        {
            cpu->cycles += 17;
            break;
        }
        default:
            assert(false);
    }
}

struct cpu8086* cpu8086_new(struct bus* bus)
{
    assert(bus);
    struct cpu8086* cpu = quick_malloc(sizeof(struct cpu8086));
    cpu->bus = bus;
    cpu8086_reset(cpu);
    return cpu;
}

void cpu8086_reset(struct cpu8086* cpu)
{
    cpu->flags = 0x0000;
    cpu->ip = 0x0000;
    cpu->cs = 0xFFFF;
    cpu->ds = 0x0000;
    cpu->ss = 0x0000;
    cpu->es = 0x0000;
    cpu->mt = true;
    cpu->q_r = 0;
    cpu->q_r = 0;

    cpu->biu_prefetch_cycles = 3;
    cpu->cycles = 0;
    cpu->current_ip = 0x0000;
    cpu8086_reset_execution_regs(cpu);
}

void cpu8086_clock(struct cpu8086* cpu)
{
    // The BIU, unless idling, is always performing instruction fetches.
    // Assume these take 4 cycles to complete each bus cycle, but Tw wait
    // states can feature in a bus cycle (between T3-T4) in the actual 808x.
    if (cpu->mt || cpu->q_w != cpu->q_r)
    {
        if (cpu->biu_prefetch_cycles == 0)
        {
            cpu->q[cpu->q_w] = bus_read_short(cpu->bus, (cpu->cs << 4) + cpu->ip);
            cpu->q_w = (cpu->q_w + 1) % 3;
            cpu->mt = false;
            if (cpu->ip & 1)
            {
                cpu->hl = 1;
                cpu->ip++;
            }
            else
                cpu->ip += 2;
        }
        cpu->biu_prefetch_cycles = (cpu->biu_prefetch_cycles - 1) % 4;
    }

    // If the last opcode was WAIT and TEST is high, stall for another 5 cycles.
    if (cpu->opcode_byte == 0x9B && cpu->test)
        cpu->cycles += 5;

    // Skip if there are remaining cycles of execution.
    if (cpu->cycles > 0)
    {
        cpu->cycles--;
        return;
    }

    // Skip if the prefetch queue is empty. (This is sort of like FC, I supppose.)
    if (cpu->mt)
        return;

    if (cpu->stage == CPU8086_EXECUTING)
        cpu8086_reset_execution_regs(cpu);

    struct opcode* op;
    if (cpu->opcode_byte != OPCODE_NONE)
        op = &op_table[cpu->opcode_byte];

next_stage: // oh dear
    switch (cpu->stage)
    {
        // Prepare for reading a new instruction.
        case CPU8086_READY:
        {
            uint8_t byte = cpu8086_prefetch_dequeue(cpu);

            // Prefix bytes take 2 cycles as they are just 1BL instructions.
            // (Should these therefore just have their own functions?)
            switch (byte)
            {
                case PREFIX_G1_LOCK: 
                {
                    // Currently doesn't do anything here, however it should 
                    // lock the bus after the 2nd cycle and is not unlocked
                    // until the first clock cycle of the next instruction.
                    cpu->cycles = 1;
                    return;
                }
                case PREFIX_G1_REPNZ:
                case PREFIX_G1_REPZ:
                {
                    cpu->repeat = true;
                    cpu->prefix_g1 = byte;
                    cpu->cycles = 1;
                    return;
                }
                case PREFIX_G2_ES:
                case PREFIX_G2_CS:
                case PREFIX_G2_SS:
                case PREFIX_G2_DS:
                {
                    cpu->prefix_g2 = byte;
                    cpu->cycles = 1;
                    return;
                }
            }

            cpu->opcode_byte = byte;
            op = &op_table[cpu->opcode_byte];
            if (cpu->repeat && !op->is_string)
                cpu->repeat = false;

            if (op->destination == LOC_RM || op->source == LOC_RM)
                cpu->stage = CPU8086_FETCH_MODRM;
            else if (op->source == LOC_IMM || op->source == LOC_IMM8)
                cpu->stage = CPU8086_FETCH_IMM;
            else if (op->destination == LOC_ADDR || op->source == LOC_ADDR || op->source == LOC_SEGOFF)
                cpu->stage = CPU8086_FETCH_ADDRESS;
            else
                cpu->stage = CPU8086_DECODE_LOC;
            goto next_stage;
        }

        // Fetch the ModRM byte and its displacement byte(s).
        case CPU8086_FETCH_MODRM:
        {
            if (cpu->mt)
                return;

            if (cpu->modrm_byte.value == MODRM_NONE)
                cpu->modrm_byte.value = cpu8086_prefetch_dequeue(cpu);
            
            bool is_disp16 = (cpu->modrm_byte.fields.mod == 0b00
                           && cpu->modrm_byte.fields.rm == 0b110)
                           || cpu->modrm_byte.fields.mod == MOD_DISP16;
            if ((cpu->modrm_byte.fields.mod == MOD_DISP8 || is_disp16)
                && cpu->disp8_byte == DISP8_NONE)
            {
                if (cpu->mt)
                    return;
                cpu->disp8_byte = cpu8086_prefetch_dequeue(cpu);
            }
            if (is_disp16 && cpu->disp16_byte == DISP16_NONE)
            {
                if (cpu->mt)
                    return;
                cpu->disp16_byte = cpu8086_prefetch_dequeue(cpu);
            }

            cpu->modrm_is_segreg = op->destination == LOC_SREG || op->source == LOC_SREG;
            cpu->reg = op->is_word
                ? (uintptr_t)cpu8086_reg_word(cpu, cpu->modrm_byte.fields.reg + 8 * cpu->modrm_is_segreg)
                : (uintptr_t)cpu8086_reg_byte(cpu, cpu->modrm_byte.fields.reg);
            
            if (cpu->modrm_byte.fields.mod == MOD_REG)
                cpu->rm = op->is_word
                   ? (uintptr_t)cpu8086_reg_word(cpu, cpu->modrm_byte.fields.reg)
                   : (uintptr_t)cpu8086_reg_byte(cpu, cpu->modrm_byte.fields.rm);
            else
            {
                unsigned prefix = DS;
                switch (cpu->modrm_byte.fields.rm)
                {
                    case 0b000:
                    {
                        cpu->rm = (uint16_t)(cpu->bx + cpu->si);
                        cpu->cycles += 7;
                        break;
                    }
                    case 0b001:
                    {
                        cpu->rm = (uint16_t)(cpu->bx + cpu->di);
                        cpu->cycles += 8;
                        break;
                    }
                    case 0b010:
                    {
                        cpu->rm = (uint16_t)(cpu->bp + cpu->si);
                        prefix = SS;
                        cpu->cycles += 8;
                        break;
                    }
                    case 0b011:
                    {
                        cpu->rm = (uint16_t)(cpu->bp + cpu->di);
                        prefix = SS;
                        cpu->cycles += 7;
                        break;
                    }
                    case 0b100:
                    {
                        cpu->rm = cpu->si;
                        cpu->cycles += 5;
                        break;
                    }
                    case 0b101:
                    {
                        cpu->rm = cpu->di;
                        cpu->cycles += 5;
                        break;
                    }
                    case 0b110:
                    {
                        if (cpu->modrm_byte.fields.mod != 0b00)
                        {
                            cpu->rm = cpu->bp;
                            prefix = SS;
                            cpu->cycles += 5;
                        }
                        else
                        {
                            cpu->rm = (cpu->disp16_byte << 8) | cpu->disp8_byte;
                            cpu->cycles += 6;
                        }
                        break;
                    }
                    case 0b111:
                    {
                        cpu->rm = cpu->bx;
                        cpu->cycles += 5;
                        break;
                    }
                }

                // This math should select between ES/CS/SS/DS.
                // The 2 cycle penalty should already be acounted for.
                if (cpu->prefix_g2 != PREFIX_G2_NONE)
                    prefix = ES + (cpu->prefix_g2 - PREFIX_G2_ES) / 8;
                
                if (cpu->modrm_byte.fields.mod == MOD_DISP16)
                {
                    cpu->rm = (uint16_t)(cpu->rm + (int16_t)((cpu->disp16_byte << 8) | cpu->disp8_byte));
                    cpu->cycles += 4;
                }
                else if (cpu->modrm_byte.fields.mod == MOD_DISP8)
                {
                    cpu->rm = (uint16_t)(cpu->rm + (int8_t)cpu->disp8_byte);
                    cpu->cycles += 4;
                }
                cpu->rm = ((*cpu8086_reg_word(cpu, prefix) << 4) + cpu->rm) & 0xFFFFF;
            }

            if (op->source == LOC_IMM || op->source == LOC_IMM8)
                cpu->stage = CPU8086_FETCH_IMM;
            else if (op->destination == LOC_ADDR || op->source == LOC_ADDR || op->source == LOC_SEGOFF)
                cpu->stage = CPU8086_FETCH_ADDRESS;
            else
                cpu->stage = CPU8086_DECODE_LOC;
            goto next_stage;
        }

        // Fetch the immediate byte(s).
        case CPU8086_FETCH_IMM:
        {
            if (cpu->imm8_byte == IMM8_NONE)
            {
                if (cpu->mt)
                    return;
                cpu->imm8_byte = cpu8086_prefetch_dequeue(cpu);
            }

            if (op->is_word && cpu->imm16_byte == IMM16_NONE)
            {
                // Opcode 0x83 works quite differently: despite it being a
                // word instruction, the immediate value read is only 8-bit.
                // In this case, it must be sign-extended.
                if (op->source == LOC_IMM8)
                    cpu->imm16_byte = 0xFF * ((cpu->imm8_byte >> 7) & 1);
                else
                {
                    if (cpu->mt)
                        return;
                    cpu->imm16_byte = cpu8086_prefetch_dequeue(cpu);
                }
            }

            cpu->immediate = op->is_word
                           ? (cpu->imm16_byte << 8) | cpu->imm8_byte
                           : cpu->imm8_byte;
            cpu->stage = CPU8086_DECODE_LOC;
            goto next_stage;
        }

        // Fetch an address. This will re-use the immediate variables.
        case CPU8086_FETCH_ADDRESS:
        {
            if (cpu->imm8_byte == IMM8_NONE)
            {
                if (cpu->mt)
                    return;
                cpu->imm8_byte = cpu8086_prefetch_dequeue(cpu);
            }

            if (cpu->imm16_byte == IMM16_NONE)
            {
                if (cpu->mt)
                    return;
                cpu->imm16_byte = cpu8086_prefetch_dequeue(cpu);
            }

            // This is used only for CALLFAR/JMPFAR (i.e. segment:offset).
            if (op->source == LOC_SEGOFF)
            {
                if (cpu->lo_segment == LO_SEGMENT_NONE)
                {
                    if (cpu->mt)
                        return;
                    cpu->lo_segment = cpu8086_prefetch_dequeue(cpu);
                }
                if (cpu->hi_segment == HI_SEGMENT_NONE)
                {
                    if (cpu->mt)
                        return;
                    cpu->hi_segment = cpu8086_prefetch_dequeue(cpu);
                }
            }

            cpu->immediate = (op->source == LOC_SEGOFF)
                           ? (cpu->imm16_byte << 24) | (cpu->imm8_byte << 16)
                           | (cpu->hi_segment << 8) | cpu->lo_segment
                           : (cpu->imm16_byte << 8) | cpu->imm8_byte;

            cpu->stage = CPU8086_DECODE_LOC;
            goto next_stage;
        }

        // Configure the destination and source addresses of the opcode.
        case CPU8086_DECODE_LOC:
        {
            loc_set(cpu, &cpu->destination, op->destination);
            loc_set(cpu, &cpu->source, op->source);
            cpu->stage = CPU8086_EXECUTING;
            goto next_stage;
        }
        
        // Execute the opcode.
        case CPU8086_EXECUTING:
        {
            assert(op->func);

            if (cpu->repeat)
                cpu->cycles += 9;

exec_op:
            if (cpu->repeat)
            {
                // No interrupt checks are carried out here (see the manual) because
                // the string instructions are repeated all in one go and aren't
                // cycle-accurate, so there's no point constantly checking for
                // interrupts, especially if this is singlethreaded anyway.

                if (cpu->cx == 0)
                    return;
                cpu->cx--;
            }

            op->func(op, cpu);

            if (cpu->repeat)
            {
                static const int delta_table[2][2] = { { 1, -1 }, { 2, -2 } };
                int delta = delta_table[op->is_word][cpu8086_getflag(cpu, FLAG_DIRECTION)];
                cpu->si += delta;
                cpu->di += delta;

                // TODO: check for CMPS/SCAS
                // const bool z = (cpu->prefix_g1 == PREFIX_G1_REPZ) ? true : false;

                goto exec_op; 
            }

            cpu->cycles -= 1;   // For simplicity's sake, I decided to just copy the
                                // cycles count of each instruction for all sets of
                                // possible operands to each instruction. However,
                                // this clock cycle would also be included, so 1 must
                                // be subtracted.
            return;
        }
    }
}

void cpu8086_free(struct cpu8086* cpu)
{
    assert(cpu);
    free(cpu);
}