#ifndef TLB_H
#define TLB_H

#include <stdint.h>
#include <stdbool.h>
#include "pico.h"

#define TLB_SIZE 64

typedef struct {
    uint64_t vpn;   // Virtual Page Number (va >> 12)
    uint32_t ppn;   // Physical Page Number (pa >> 12)
    uint8_t  perm;  // R/W/X Permissions
    bool     valid;
} TLBEntry;

void tlb_init(void);
void tlb_flush(void);
bool __not_in_flash_func(tlb_lookup)(uint64_t vpn, uint8_t access, uint32_t *ppn_out);
void __not_in_flash_func(tlb_insert)(uint64_t vpn, uint32_t ppn, uint8_t perm);

#endif