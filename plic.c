#include "plic.h"
#include <stdio.h>

uint32_t g_plic_virtio_pending = 0;
extern RV64CPU *g_cpu;

bool plic_is_addr(uint32_t paddr) {
    return (paddr >= PLIC_BASE && paddr < (PLIC_BASE + PLIC_SIZE));
}

uint32_t plic_read32(uint32_t paddr) {
    uint32_t offset = paddr - PLIC_BASE;
    // S-mode Hart 0 Claim レジスタ
    if (offset == 0x201004) {
        if (g_plic_virtio_pending) {
            // ★ Claim (読み出し) が行われたら即座に Pending と CPU の SEIP フラグをクリア
            g_plic_virtio_pending = 0;
            if (g_cpu) {
                g_cpu->mip.lo &= ~(1U << 9); // SEIP (Bit 9) を下げる
            }
            return 1; // VirtIO IRQ (1)
        }
        return 0;
    }
    return 0;
}

void plic_write32(uint32_t paddr, uint32_t val, RV64CPU *cpu) {
    uint32_t offset = paddr - PLIC_BASE;
    if (offset == 0x201004) {
        // Complete 完了通知（必要に応じて拡張）
    }
}