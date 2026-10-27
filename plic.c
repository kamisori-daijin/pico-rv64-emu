#include "plic.h"
#include <stdio.h>

uint32_t g_plic_virtio_pending = 0;

bool plic_is_addr(uint32_t paddr) {
    return (paddr >= PLIC_BASE && paddr < (PLIC_BASE + PLIC_SIZE));
}

uint32_t plic_read32(uint32_t paddr) {
    uint32_t offset = paddr - PLIC_BASE;
    // S-mode Hart 0 Claim / Complete レジスタ
    if (offset == 0x201004) {
        if (g_plic_virtio_pending) {
            return 1; // VirtIO IRQ (1)
        }
        return 0;
    }
    return 0;
}

void plic_write32(uint32_t paddr, uint32_t val, RV64CPU *cpu) {
    uint32_t offset = paddr - PLIC_BASE;
    if (offset == 0x201004) {
        if (val == 1) { // 割り込み完了通知 (Complete)
            g_plic_virtio_pending = 0;
            cpu->mip.lo &= ~(1U << 9); // SEIP (Supervisor External Interrupt) をクリア
        }
    }
}