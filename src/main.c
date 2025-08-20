// floason (C) 2025
// Licensed under the MIT License.

#include <stdio.h>
#include <memory.h>

#include "flex_version.h"
#include "bus.h"

int main()
{
    printf("Hello world\n%ld\n%s\n%d.%d.%d\n", __STDC_VERSION__, GIT_HASH, MAJOR, MINOR, PATCH);
    struct bus* pc = bus_new(0x100000);

    /*
    // IBM PC/XT May 1986 BIOS FFFF:0000 paragraph
    uint8_t buffer[] = { 0xEA, 0x5B, 0xE0, 0x00, 0xF0, 0x30, 0x35, 0x2F, 
                         0x30, 0x39, 0x2F, 0x38, 0x36, 0xCC, 0xFB, 0x12 };
                         */
    
    // ADD [BX + SI - 1], CX
    /*
    uint8_t buffer[] = { 0x01, 0x48, 0xFF, 0xFF };
    memcpy(&pc->memory[0xFFFF0], &buffer, sizeof(buffer));
    */

    // ADD AX, 1003H
    uint8_t buffer[] = { 0x05, 0x03, 0x10 };
    memcpy(&pc->memory[0xFFFF0], &buffer, sizeof(buffer));

    pc->cpu->ax = 0xFFFF;
    pc->cpu->cx = 300;
    pc->cpu->bx = 1;
    for (;;)
    {
        bus_clock(pc);
    }

    bus_free(pc);
    return 0;
}