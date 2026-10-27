#include "mmu.h"
#include <stdio.h>

// main.c にある物理メモリ直引き用プロトタイプ宣言
extern uint32_t mem_read32_phys(uint32_t paddr);
extern reg64_t  mem_read64_phys(uint32_t paddr);

// PTE フラグ定義
#define PTE_V (1U << 0) // Valid
#define PTE_R (1U << 1) // Read
#define PTE_W (1U << 2) // Write
#define PTE_X (1U << 3) // Execute
#define PTE_U (1U << 4) // User

bool mmu_translate(RV64CPU *cpu, uint32_t va_lo, uint32_t va_hi, AccessType access, uint32_t *pa_out) {
    uint64_t satp_64 = ((uint64_t)cpu->satp.hi << 32) | cpu->satp.lo;
    uint32_t mode = (uint32_t)(satp_64 >> 60) & 0xF;

    // 1. Mモード (priv_mode == 3) または satp.MODE != 8 (Sv39無効) の場合は直引き
    if (cpu->priv_mode == 3 || mode != 8) {
        *pa_out = va_lo; // 物理アドレス空間（0x80000000〜）そのまま
        return true;
    }

    uint64_t va_64 = ((uint64_t)va_hi << 32) | va_lo;

    // Sv39: 仮想アドレス 39-bit の分解
    uint32_t vpn[3];
    vpn[2] = (va_64 >> 30) & 0x1FF;
    vpn[1] = (va_64 >> 21) & 0x1FF;
    vpn[0] = (va_64 >> 12) & 0x1FF;
    uint32_t page_offset = va_64 & 0xFFF;

    // ルートページテーブルの物理アドレス計算 (satp.PPN << 12)
    uint64_t ppn = satp_64 & 0x000FFFFFFFFFFFFULL;
    uint32_t a = (uint32_t)(ppn << 12);

    // 2. 3レベル (L2 -> L1 -> L0) の Page Table Walk
    for (int i = 2; i >= 0; i--) {
        uint32_t pte_addr = a + (vpn[i] * 8);
        reg64_t pte_reg = mem_read64_phys(pte_addr);
        uint64_t pte = ((uint64_t)pte_reg.hi << 32) | pte_reg.lo;

        // V ビット判定
        if ((pte & PTE_V) == 0) {
            return false; // Invalid Page
        }

        // R/W/X フラグチェック
        bool r = (pte & PTE_R) != 0;
        bool w = (pte & PTE_W) != 0;
        bool x = (pte & PTE_X) != 0;

        if (!r && w) return false; // 無効なフラグ組み合わせ

        if (r || x) {
            // リーフ PTE を発見
            uint64_t pte_ppn = (pte >> 10) & 0x000FFFFFFFFFFFFULL;

            if (i == 0) {
                // 4KB ページ
                *pa_out = (uint32_t)((pte_ppn << 12) | page_offset);
            } else if (i == 1) {
                // 2MB メガページ (xv6 のカーネルマッピングで使用)
                uint32_t offset_2m = va_64 & 0x1FFFFF;
                *pa_out = (uint32_t)(((pte_ppn >> 9) << 21) | offset_2m);
            } else {
                // 1GB ギガページ
                uint32_t offset_1g = va_64 & 0x3FFFFFFF;
                *pa_out = (uint32_t)(((pte_ppn >> 18) << 30) | offset_1g);
            }

            return true;
        }

        // 次のレベルのページテーブルアドレスへ進む
        uint64_t next_ppn = (pte >> 10) & 0x000FFFFFFFFFFFFULL;
        a = (uint32_t)(next_ppn << 12);
    }

    return false;
}