#include "plic.h"
#include <stdio.h>

uint32_t g_plic_virtio_pending = 0;
uint32_t g_plic_uart_pending   = 0; // ★ UART割り込みフラグ追加
extern RV64CPU *g_cpu;

bool plic_is_addr(uint32_t paddr) {
    return (paddr >= PLIC_BASE && paddr < (PLIC_BASE + PLIC_SIZE));
}

uint32_t plic_read32(uint32_t paddr) {
    uint32_t offset = paddr - PLIC_BASE;
    // S-mode Hart 0 Claim レジスタ
    if (offset == 0x201004) {
        // ★ UART (IRQ 10) の割込みを優先処理
        if (g_plic_uart_pending) {
            g_plic_uart_pending = 0;
            if (g_cpu) {
                // 他に VirtIO 割り込みが残っていなければ SEIP (Bit 9) をクリア
                if (!g_plic_virtio_pending) {
                    g_cpu->mip.lo &= ~(1U << 9);
                }
            }
            return 10; // UART IRQ (10)
        }
        // VirtIO (IRQ 1)
        if (g_plic_virtio_pending) {
            g_plic_virtio_pending = 0;
            if (g_cpu) {
                if (!g_plic_uart_pending) {
                    g_cpu->mip.lo &= ~(1U << 9);
                }
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
        // Complete (完了通知)
    }
}