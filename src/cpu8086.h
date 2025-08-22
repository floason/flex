// floason (C) 2025
// Licensed under the MIT License.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "bus.h"

// The following registers are valid for ModRM.
#define AX              0b000
#define CX              0b001
#define DX              0b010
#define BX              0b011
#define SP              0b100
#define BP              0b101
#define SI              0b110
#define DI              0b111

#define ES              0b1000
#define CS              0b1001
#define SS              0b1010
#define DS              0b1011

#define REGISTER_COUNT  0b1100

#define FLAG_CARRY      (1 << 0)
#define FLAG_PARITY     (1 << 2)
#define FLAG_AUXILIARY  (1 << 4)
#define FLAG_ZERO       (1 << 6)
#define FLAG_SIGN       (1 << 7)
#define FLAG_TRAP       (1 << 8)
#define FLAG_INTENABLE  (1 << 9)
#define FLAG_DIRECTION  (1 << 10)
#define FLAG_OVERFLOW   (1 << 11)

#define MOD_FIELD       2
#define REG_FIELD       3
#define RM_FIELD        3

#define MOD_INDIRECT    0b00
#define MOD_DISP8       0b01
#define MOD_DISP16      0b10
#define MOD_REG         0b11

#define PREFIX_G1_NONE  0x00
#define PREFIX_G1_LOCK  0xF0
#define PREFIX_G1_REPNZ 0xF2
#define PREFIX_G1_REPZ  0xF3

#define PREFIX_G2_NONE  0x00
#define PREFIX_G2_ES    0x26
#define PREFIX_G2_CS    0x2E
#define PREFIX_G2_SS    0x36
#define PREFIX_G2_DS    0x3E

#define OPCODE_NONE     0xFFFF
#define MODRM_NONE      0xFFFF
#define DISP8_NONE      0xFFFF
#define DISP16_NONE     0xFFFF
#define IMM8_NONE       0xFFFF
#define IMM16_NONE      0xFFFF

enum cpu8086_stage
{
    CPU8086_READY,
    CPU8086_FETCH_MODRM,
    CPU8086_FETCH_IMM,
    CPU8086_ADDRESS_MODE,
    CPU8086_EXECUTING
};

enum location_type
{
    DECODED_REGISTER,
    DECODED_MEMORY,
    DECODED_IMMEDIATE,
    DECODED_SEGREG
};

struct location
{
    enum location_type type;
    uintptr_t address;
    bool virtual;
};

struct cpu8086
{
    struct bus* bus;

    // General purpose register - accumulator.
    union
    {
        struct
        {
            uint8_t al;
            uint8_t ah;
        };
        uint16_t ax;
    };

    // General purpose register - count register.
    union
    {
        struct
        {
            uint8_t cl;
            uint8_t ch;
        };
        uint16_t cx;
    };

    // General purpose register - data register.
    union
    {
        struct
        {
            uint8_t dl;
            uint8_t dh;
        };
        uint16_t dx;
    };

    // General purpose register - base register.
    union
    {
        struct
        {
            uint8_t bl;
            uint8_t bh;
        };
        uint16_t bx;
    };
    
    // Offset registers.
    uint16_t sp;                    // Stack pointer.
    uint16_t bp;                    // Base pointer.
    uint16_t si;                    // Source index. 
    uint16_t di;                    // Destination index.

    // Segment registers.
    uint16_t es;                    // Extra segment.
    uint16_t cs;                    // Code segment.
    uint16_t ss;                    // Stack segment.
    uint16_t ds;                    // Data segment.
    
    // Other registers.
    uint16_t ip;                    // Instruction pointer.
    uint16_t flags;                 // Status register.

    // Prefetch (instruction) queue bus.
    uint16_t q[3];                  // Q0-Q2.
    uint8_t q_r             : 2;    // Used for reading the next queued word.
    uint8_t q_w             : 2;    // Used for writing the next word onto the queue.
    uint8_t hl              : 1;    // 0 - lo-byte of word; 1 - hi-byte of word
    uint8_t mt              : 1;    // Is the queue empty?

    // Emulation execution variables.
    uint16_t current_ip;            // The current instruction pointer, irrespective of the prefetch queue.
    uint8_t cycles;                 // How many cycles must the CPU pause for?
    uint8_t biu_prefetch_cycles;    // How many cycles remaining until the prefetch finishes?
    uint8_t prefix_g1;              // Group 1 prefix (if any).
    uint8_t prefix_g2;              // Group 2 prefix (if any).
    uint16_t opcode_byte;           // The actual byte for the opcode itself (this may also be an extension byte).
    uint16_t disp8_byte;            // Disp8 byte.
    uint16_t disp16_byte;           // Disp16 byte.
    uint16_t imm8_byte;             // Imm8 byte.
    uint16_t imm16_byte;            // Imm16 byte.
    uint16_t immediate;             // Calculated immediate.
    uintptr_t rm;                   // Calculated rm during ModRM stage.
    uintptr_t reg;                  // Calculated reg during ModRM stage.
    enum cpu8086_stage stage;       // Current stage of instruction byte fetching.
    union
    {
        struct
        {
            uint8_t rm      : 3;
            uint8_t reg     : 3;
            uint8_t mod     : 2;
            uint8_t padding;
        } fields;
        uint16_t value;
    } modrm_byte;                   // ModRM byte.
    struct location destination;    // Destination of the opcode.
    struct location source;         // Source of the opcode.
    bool modrm_is_segreg;           // Does the ModRM byte use segreg?
};

struct cpu8086* cpu8086_new(struct bus* bus);
void cpu8086_reset(struct cpu8086* cpu);
void cpu8086_clock(struct cpu8086* cpu);
void cpu8086_free(struct cpu8086* cpu);