#ifndef CLINT_H
#define CLINT_H

#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"

#define CLINT_BASE 0x02000000ULL
#define CLINT_SIZE 0x10000ULL

bool clint_is_addr(uint32_t paddr);
uint32_t clint_read32(uint32_t paddr);
void clint_write32(uint32_t paddr, uint32_t val);

// MITP Update func
void clint_update_timer_interrupt(RV64CPU *cpu);

#endif