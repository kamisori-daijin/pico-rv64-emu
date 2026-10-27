#ifndef PLIC_H
#define PLIC_H

#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"

#define PLIC_BASE 0x0C000000U
#define PLIC_SIZE 0x00400000U

extern uint32_t g_plic_virtio_pending;

bool plic_is_addr(uint32_t paddr);
uint32_t plic_read32(uint32_t paddr);
void plic_write32(uint32_t paddr, uint32_t val, RV64CPU *cpu);

#endif