#include "tlb.h"

static TLBEntry g_tlb[TLB_SIZE];

void tlb_init(void) {
    tlb_flush();
}

void tlb_flush(void) {
    for (int i = 0; i < TLB_SIZE; i++) {
        g_tlb[i].valid = false;
    }
}

bool __not_in_flash_func(tlb_lookup)(uint64_t vpn, uint8_t access, uint32_t *ppn_out) {
    uint32_t idx = (uint32_t)(vpn & (TLB_SIZE - 1));
    TLBEntry *entry = &g_tlb[idx];

    if (entry->valid && entry->vpn == vpn) {
        if ((entry->perm & (1U << access)) != 0) {
            *ppn_out = entry->ppn;
            return true;
        }
    }
    return false;
}

void __not_in_flash_func(tlb_insert)(uint64_t vpn, uint32_t ppn, uint8_t perm) {
    uint32_t idx = (uint32_t)(vpn & (TLB_SIZE - 1));
    g_tlb[idx].vpn = vpn;
    g_tlb[idx].ppn = ppn;
    g_tlb[idx].perm = perm;
    g_tlb[idx].valid = true;
}