// floason (C) 2025
// Licensed under the MIT License.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "cpu8086.h"

struct bus
{
    struct cpu8086* cpu;
    uint8_t* memory;
    size_t memory_size;
    bool memory_ready;

    // Master clock division.
    int cpu_clock;
};

struct bus* bus_new(size_t memory);
uint8_t bus_read_byte(struct bus* bus, uintptr_t address);
uint16_t bus_read_short(struct bus* bus, uintptr_t address);
void bus_write_byte(struct bus* bus, uintptr_t address, uint8_t data);
void bus_write_short(struct bus* bus, uintptr_t address, uint16_t data);
void bus_clock(struct bus* bus);
void bus_free(struct bus* bus);