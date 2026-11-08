#include "plic.h"
#include <stdio.h>

// ★ フラグの実体定義
uint32_t g_plic_uart_pending = 0;
uint32_t g_plic_virtio_pending = 0;

bool plic_is_addr(uint32_t paddr) {
    return (paddr >= PLIC_BASE && paddr < (PLIC_BASE + PLIC_SIZE));
}

uint32_t plic_read32(uint32_t paddr) {
    uint32_t offset = paddr - PLIC_BASE;
    if (offset == 0x001000) { // Pending Interrupt 1 (UART=1, VirtIO=2)
        uint32_t pending = 0;
        if (g_plic_uart_pending) pending |= (1U << 1);
        if (g_plic_virtio_pending) pending |= (1U << 2);
        return pending;
    }
    if (offset == 0x200004) { // Claim / Complete
        if (g_plic_uart_pending) return 1;
        if (g_plic_virtio_pending) return 2;
        return 0;
    }
    return 0;
}

void plic_write32(uint32_t paddr, uint32_t val, RV64CPU *cpu) {
    uint32_t offset = paddr - PLIC_BASE;
    if (offset == 0x200004) { // Complete (割り込み処理完了)
        if (val == 1) {
            g_plic_uart_pending = 0;
        } else if (val == 2) {
            g_plic_virtio_pending = 0;
        }
        if (!g_plic_uart_pending && !g_plic_virtio_pending && cpu) {
            __atomic_fetch_and(&cpu->mip.lo, ~(1U << 9), __ATOMIC_RELAXED);
        }
    }
}