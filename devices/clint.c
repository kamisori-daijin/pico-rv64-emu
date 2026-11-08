#include "clint.h"
#include "pico/stdlib.h"

static volatile uint64_t mtimecmp = 0xFFFFFFFFFFFFFFFFULL;

bool clint_is_addr(uint32_t paddr) {
    return (paddr >= CLINT_BASE && paddr < (CLINT_BASE + CLINT_SIZE));
}

uint32_t clint_read32(uint32_t paddr) {
    uint32_t offset = paddr - CLINT_BASE;
    uint64_t cur_mtimecmp = __atomic_load_n(&mtimecmp, __ATOMIC_RELAXED);
    if (offset == 0x0040) {
        uint64_t t = time_us_64();
        return (uint32_t)t;
    }
    if (offset == 0x0044) {
        uint64_t t = time_us_64();
        return (uint32_t)(t >> 32);
    }
    if (offset == 0x4000) return (uint32_t)cur_mtimecmp;
    if (offset == 0x4004) return (uint32_t)(cur_mtimecmp >> 32);
    return 0;
}

void clint_write32(uint32_t paddr, uint32_t val) {
    uint32_t offset = paddr - CLINT_BASE;
    uint64_t cur = __atomic_load_n(&mtimecmp, __ATOMIC_RELAXED);
    if (offset == 0x4000) {
        cur = (cur & 0xFFFFFFFF00000000ULL) | val;
    } else if (offset == 0x4004) {
        cur = (cur & 0x00000000FFFFFFFFULL) | ((uint64_t)val << 32);
    }
    __atomic_store_n(&mtimecmp, cur, __ATOMIC_RELAXED);
}

void clint_update_timer_interrupt(RV64CPU *cpu) {
    uint64_t mtime = time_us_64();
    uint64_t cur_mtimecmp = __atomic_load_n(&mtimecmp, __ATOMIC_RELAXED);
    if (cur_mtimecmp != 0 && mtime >= cur_mtimecmp) {
        __atomic_fetch_or(&cpu->mip.lo, (1U << 7) | (1U << 5), __ATOMIC_RELAXED);
    } else {
        __atomic_fetch_and(&cpu->mip.lo, ~((1U << 7) | (1U << 5)), __ATOMIC_RELAXED);
    }
}