#include "mmu.h"
#include <stdio.h>

extern uint32_t mem_read32_phys(uint32_t paddr);
extern reg64_t  mem_read64_phys(uint32_t paddr);

#define PTE_V (1U << 0)
#define PTE_R (1U << 1)
#define PTE_W (1U << 2)
#define PTE_X (1U << 3)
#define PTE_U (1U << 4)

bool mmu_translate(RV64CPU *cpu, uint32_t va_lo, uint32_t va_hi, AccessType access, uint32_t *pa_out) {
    uint64_t satp_64 = ((uint64_t)cpu->satp.hi << 32) | cpu->satp.lo;
    uint32_t mode = (uint32_t)(satp_64 >> 60) & 0xF;

    // In M-mode, or when satp.MODE != 8 (Sv39 enabled), physical addresses are accessed directly.
    if (cpu->priv_mode == 3 || mode != 8) {
        *pa_out = va_lo;
        return true;
    }

    uint64_t va_64 = ((uint64_t)va_hi << 32) | va_lo;

    uint32_t vpn[3];
    vpn[2] = (va_64 >> 30) & 0x1FF;
    vpn[1] = (va_64 >> 21) & 0x1FF;
    vpn[0] = (va_64 >> 12) & 0x1FF;
    uint32_t page_offset = va_64 & 0xFFF;

    uint64_t ppn = satp_64 & 0x000FFFFFFFFFFFFULL;
    uint64_t a = ppn << 12; // ★ uint64_t 

    for (int i = 2; i >= 0; i--) {
        uint64_t pte_addr = a + (vpn[i] * 8);
        reg64_t pte_reg = mem_read64_phys((uint32_t)pte_addr);
        uint64_t pte = ((uint64_t)pte_reg.hi << 32) | pte_reg.lo;

        // V Bit 
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

        // Access permission verification upon reaching a leaf entry (r || x)
        if (r || x) {
            // Kernel page access protection check in U-mode
            if (cpu->priv_mode == 0 && !u) {
                return false;
            }
            // User page access check in S-mode (basic check, ignoring the SUM bit)
            if (cpu->priv_mode == 1 && u) {
                // SUM checks can be added as needed.
            }

            // Permission verification by access type
            if (access == FETCH_EXEC && !x) return false;
            if (access == FETCH_READ && !r) return false;
            if (access == FETCH_WRITE && !w) return false;

            uint64_t pte_ppn = (pte >> 10) & 0x000FFFFFFFFFFFFULL;

            if (i == 0) {
                *pa_out = (uint32_t)((pte_ppn << 12) | page_offset);
            } else if (i == 1) {
                uint32_t offset_2m = va_64 & 0x1FFFFF;
                *pa_out = (uint32_t)(((pte_ppn >> 9) << 21) | offset_2m);
            } else {
                uint32_t offset_1g = va_64 & 0x3FFFFFFF;
                *pa_out = (uint32_t)(((pte_ppn >> 18) << 30) | offset_1g);
            }

            return true;
        }

        // Mid table pointer
        uint64_t next_ppn = (pte >> 10) & 0x000FFFFFFFFFFFFULL;
        a = next_ppn << 12; // ★ uint64_t 
    }

    return false;
}