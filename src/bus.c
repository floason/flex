// floason (C) 2025
// Licensed under the MIT License.

#include <assert.h>

#include "bus.h"
#include "util.h"

struct bus* bus_new(size_t memory)
{
    struct bus* bus = (struct bus*)quick_malloc(sizeof(struct bus));
    bus->cpu = cpu8086_new(bus);
    bus->memory = (uint8_t*)quick_malloc(memory);
    bus->memory_size = memory;
    return bus;
}

uint8_t bus_read_byte(struct bus* bus, uintptr_t address)
{
    return bus->memory[address & 0xFFFFF];
}

uint16_t bus_read_short(struct bus* bus, uintptr_t address)
{
    return bus->memory[address & 0xFFFFF] | (bus->memory[(address + 1) & 0xFFFFF] << 8);
}

void bus_write_byte(struct bus* bus, uintptr_t address, uint8_t data)
{
    bus->memory[address & 0xFFFFF] = data;
}

void bus_write_short(struct bus* bus, uintptr_t address, uint16_t data)
{
    bus->memory[address & 0xFFFFF] = data & 0xFF;
    bus->memory[(address + 1) & 0xFFFFF] = data >> 8;
}

void bus_clock(struct bus* bus)
{
    // Assume master clock division akin to the IBM PC for now.
    if (bus->cpu_clock == 0)
        cpu8086_clock(bus->cpu);
    bus->cpu_clock = (bus->cpu_clock - 1) % 3;
}

void bus_free(struct bus* bus)
{
    assert(bus);
    cpu8086_free(bus->cpu);
    free(bus->memory);
    free(bus);
}