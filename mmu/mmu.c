#include "mmu.h"
#include "tlb.h"  
#include <stdio.h>

extern uint32_t mem_read32_phys(uint32_t paddr);
extern reg64_t  mem_read64_phys(uint32_t paddr);

#define PTE_V (1U << 0)
#define PTE_R (1U << 1)
#define PTE_W (1U << 2)
#define PTE_X (1U << 3)
#define PTE_U (1U << 4)

bool __not_in_flash_func(mmu_translate)(RV64CPU *cpu, uint32_t va_lo, uint32_t va_hi, AccessType access, uint32_t *pa_out) {
    uint64_t satp_64 = ((uint64_t)cpu->satp.hi << 32) | cpu->satp.lo;
    uint32_t mode = (uint32_t)(satp_64 >> 60) & 0xF;

    // Mモード、または satp.MODE != 8 (Sv39無効) の場合は物理アドレス直結
    if (cpu->priv_mode == 3 || mode != 8) {
        *pa_out = va_lo;
        return true;
    }

    uint64_t va_64 = ((uint64_t)va_hi << 32) | va_lo;
    uint64_t vpn_val = va_64 >> 12;
    uint32_t page_offset = va_64 & 0xFFF;

    // ★ 1. TLBルックアップ (超高速パス)
    uint32_t tlb_ppn = 0;
    if (tlb_lookup(vpn_val, (uint8_t)access, &tlb_ppn)) {
        *pa_out = (tlb_ppn << 12) | page_offset;
        return true;
    }

    // ★ 2. TLBミス時のみ従来のページウォークを実行
    uint32_t vpn[3];
    vpn[2] = (va_64 >> 30) & 0x1FF;
    vpn[1] = (va_64 >> 21) & 0x1FF;
    vpn[0] = (va_64 >> 12) & 0x1FF;

    uint64_t ppn = satp_64 & 0x000FFFFFFFFFFFFULL;
    uint64_t a = ppn << 12;

    for (int i = 2; i >= 0; i--) {
        uint64_t pte_addr = a + (vpn[i] * 8);
        reg64_t pte_reg = mem_read64_phys((uint32_t)pte_addr);
        uint64_t pte = ((uint64_t)pte_reg.hi << 32) | pte_reg.lo;

        // V Bit チェック
        if ((pte & PTE_V) == 0) {
            return false;
        }

        bool r = (pte & PTE_R) != 0;
        bool w = (pte & PTE_W) != 0;
        bool x = (pte & PTE_X) != 0;
        bool u = (pte & PTE_U) != 0;

        if (!r && w) {
            return false;
        }

        // リーフPTE到達時の権限チェック
        if (r || x) {
            if (cpu->priv_mode == 0 && !u) return false;
            
            if (access == FETCH_EXEC && !x) return false;
            if (access == FETCH_READ && !r) return false;
            if (access == FETCH_WRITE && !w) return false;

            uint64_t pte_ppn = (pte >> 10) & 0x000FFFFFFFFFFFFULL;
            uint32_t final_ppn = 0;

            if (i == 0) {
                final_ppn = (uint32_t)pte_ppn;
                *pa_out = (final_ppn << 12) | page_offset;
            } else if (i == 1) {
                uint32_t offset_2m = va_64 & 0x1FFFFF;
                final_ppn = (uint32_t)((pte_ppn >> 9) << 9) | vpn[0];
                *pa_out = (final_ppn << 12) | page_offset;
            } else {
                uint32_t offset_1g = va_64 & 0x3FFFFFFF;
                final_ppn = (uint32_t)((pte_ppn >> 18) << 18) | (vpn[1] << 9) | vpn[0];
                *pa_out = (final_ppn << 12) | page_offset;
            }

            // ★ 3. 変換成功結果を TLB に登録して次回から爆速化
            // AccessType (EXEC=0, READ=1, WRITE=2) に対応するビットフラグを作成
            uint8_t perm = (x ? (1U << FETCH_EXEC) : 0) |
                           (r ? (1U << FETCH_READ) : 0) |
                           (w ? (1U << FETCH_WRITE) : 0);
            tlb_insert(vpn_val, final_ppn, perm);

            return true;
        }

        // 次のレベルのページテーブルへ
        uint64_t next_ppn = (pte >> 10) & 0x000FFFFFFFFFFFFULL;
        a = next_ppn << 12;
    }

    return false;
}