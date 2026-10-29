#ifndef MMU_H
#define MMU_H

#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"

// Access type
typedef enum {
    FETCH_EXEC, // Fetch(X)
    FETCH_READ, // Read(R)
    FETCH_WRITE // Write (W)
} AccessType;

// Function to translate a virtual address (VA) to a physical address (PA)
// Returns: true on success, false on failure (e.g., due to a page fault)
bool mmu_translate(RV64CPU *cpu, uint32_t va_lo, uint32_t va_hi, AccessType access, uint32_t *pa_out);

#endif