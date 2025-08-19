// floason (C) 2025
// Licensed under the MIT License.

// I'm not intending for this to be super accurate, considering 
// this is the first time I'm writing an 808x emulator and I don't
// really have strong EE knowledge.

#include <assert.h>

#include "cpu8086.h"
#include "util.h"

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

    // ModRM
    LOC_RM,
    LOC_REG,
    LOC_SREG,

    LOC_NULL
};

struct opcode
{
    const char* name;
    uint8_t min_cycles;
    enum opcode_location destination;
    enum opcode_location source;
    bool is_word;
    void (*func)(struct opcode* op, struct cpu8086* cpu);
};

static struct opcode op_table[] = 
{
    // 0x00 to 0x0F
    { "ADD", 0, LOC_RM, LOC_REG, false, NULL }
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
    return (uint8_t*)(&cpu->ax) + (reg & 0b11) * 2 + reg / 4;
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
    return read;
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
        case LOC_ES:
        case LOC_CS:
        case LOC_SS:
        case LOC_DS:
        {
            loc->type = DECODED_REGISTER;
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
            loc->type = DECODED_REGISTER;
            loc->address = (uintptr_t)cpu8086_reg_byte(cpu, (unsigned)type);
            loc->virtual = false;
            break;
        }
        case LOC_IMM:
        {
            loc->type = DECODED_IMMEDIATE;
            loc->address = (uintptr_t)&cpu->immediate;
            loc->virtual = false;
            break;
        }
        case LOC_RM:
        {
            loc->type = (cpu->modrm_byte.fields.mod == MOD_REG)
                      ? DECODED_REGISTER
                      : DECODED_MEMORY;
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
        case LOC_NULL:
            break;
    }
}

static inline void cpu8086_reset_execution_regs(struct cpu8086* cpu)
{
    cpu->prefix_g1 = PREFIX_G1_NONE;
    cpu->prefix_g2 = PREFIX_G1_NONE;
    cpu->opcode_byte = OPCODE_NONE;
    cpu->disp8_byte = DISP8_NONE;
    cpu->disp16_byte = DISP16_NONE;
    cpu->imm8_byte = IMM8_NONE;
    cpu->imm16_byte = IMM16_NONE;
    cpu->stage = CPU8086_READY;
    cpu->modrm_byte.value = MODRM_NONE;
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
    cpu->flags = 0;
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

    // Skip if there are remaining cycles of execution.
    if (cpu->cycles > 0)
        return;

    // Skip if the prefetch queue is empty. (This is sort of like FC, I supppose.)
    if (cpu->mt)
        return;

    struct opcode op;
    if (cpu->opcode_byte != OPCODE_NONE)
        op = op_table[cpu->opcode_byte];

next_stage: // oh dear
    switch (cpu->stage)
    {
        // Prepare for reading a new instruction.
        case CPU8086_READY:
        {
            uint8_t byte = cpu8086_prefetch_dequeue(cpu);

            // Prefix bytes take 2 cycles.
            switch (byte)
            {
                case PREFIX_G1_LOCK: 
                    // Currently doesn't do anything here, however it should 
                    // lock the bus after the 2nd cycle and is not unlocked
                    // until the first clock cycle of the next instruction.
                case PREFIX_G1_REPNZ:
                case PREFIX_G1_REPZ:
                {
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
            op = op_table[cpu->opcode_byte];
            if (op.destination == LOC_RM || op.source == LOC_RM)
                cpu->stage = CPU8086_FETCH_MODRM;
            else if (op.source == LOC_IMM)
                cpu->stage = CPU8086_FETCH_IMM;
            else
                cpu->stage = CPU8086_ADDRESS_MODE;
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

            bool segreg = op.destination == LOC_SREG || op.source == LOC_SREG;
            cpu->reg = op.is_word
                ? (uintptr_t)cpu8086_reg_word(cpu, cpu->modrm_byte.fields.reg + 8 * segreg)
                : (uintptr_t)cpu8086_reg_byte(cpu, cpu->modrm_byte.fields.reg);
            
            if (cpu->modrm_byte.fields.mod == MOD_REG)
                cpu->rm = op.is_word
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

            if (op.source == LOC_IMM)
                cpu->stage = CPU8086_FETCH_IMM;
            else
                cpu->stage = CPU8086_ADDRESS_MODE;
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

            if (op.is_word)
            {
                if (cpu->mt)
                    return;
                cpu->imm16_byte = cpu8086_prefetch_dequeue(cpu);
            }

            cpu->immediate = op.is_word
                           ? (cpu->imm16_byte << 8) | cpu->imm8_byte
                           : cpu->imm8_byte;
            cpu->stage = CPU8086_ADDRESS_MODE;
            goto next_stage;
        }

        // Configure the destination and source addresses of the opcode.
        case CPU8086_ADDRESS_MODE:
        {
            loc_set(cpu, &cpu->destination, op.destination);
            loc_set(cpu, &cpu->source, op.source);
            cpu->stage = CPU8086_EXECUTING;
            goto next_stage;
        }
        
        // Execute the opcode.
        case CPU8086_EXECUTING:
        {
            assert(op.func);
            op.func(&op, cpu);
            cpu8086_reset_execution_regs(cpu);
            return;
        }
    }
}

void cpu8086_free(struct cpu8086* cpu)
{
    assert(cpu);
    free(cpu);
}