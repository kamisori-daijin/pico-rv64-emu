#include "clint.h"
#include "pico/stdlib.h"

static uint64_t mtimecmp = 0;

bool clint_is_addr(uint32_t paddr) {
    return (paddr >= CLINT_BASE && paddr < (CLINT_BASE + CLINT_SIZE));
}

uint32_t clint_read32(uint32_t paddr) {
    uint32_t offset = paddr - CLINT_BASE;
    if (offset == 0x0040) { // mtime (lower 32-bit)
        uint64_t t = time_us_64();
        return (uint32_t)t;
    }
    if (offset == 0x0044) { // mtime (upper 32-bit)
        uint64_t t = time_us_64();
        return (uint32_t)(t >> 32);
    }
    if (offset == 0x4000) { // mtimecmp (lower)
        return (uint32_t)mtimecmp;
    }
    if (offset == 0x4004) { // mtimecmp (upper)
        return (uint32_t)(mtimecmp >> 32);
    }
    return 0;
}

void clint_write32(uint32_t paddr, uint32_t val) {
    uint32_t offset = paddr - CLINT_BASE;
    if (offset == 0x4000) {
        mtimecmp = (mtimecmp & 0xFFFFFFFF00000000ULL) | val;
    } else if (offset == 0x4004) {
        mtimecmp = (mtimecmp & 0x00000000FFFFFFFFULL) | ((uint64_t)val << 32);
    }
}