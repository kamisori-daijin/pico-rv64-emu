#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include "cpu/cpu.h"

#define RAM_SIZE (440 * 1024)      
#define RAM_BASE 0x80000000ULL

#define FLASH_BASE 0x20000000ULL

extern uint8_t guest_ram[RAM_SIZE];
extern RV64CPU *g_cpu;

// Physical memory access
uint8_t mem_read8_phys(uint32_t paddr);
uint16_t mem_read16_phys(uint32_t paddr);
uint32_t mem_read32_phys(uint32_t paddr);
reg64_t mem_read64_phys(uint32_t paddr);

void mem_write8_phys(uint32_t paddr, uint8_t val);
void mem_write16_phys(uint32_t paddr, uint16_t val);
void mem_write32_phys(uint32_t paddr, uint32_t val);
void mem_write64_phys(uint32_t paddr, reg64_t val);

// Virtural Memory access
uint16_t mem_fetch16(uint32_t vaddr);
uint32_t mem_fetch32(uint32_t vaddr);

uint8_t mem_read8(uint32_t vaddr);
uint16_t mem_read16(uint32_t vaddr);
uint32_t mem_read32(uint32_t vaddr);
reg64_t mem_read64(uint32_t vaddr);

void mem_write8(uint32_t vaddr, uint8_t val);
void mem_write16(uint32_t vaddr, uint16_t val);
void mem_write32(uint32_t vaddr, uint32_t val);
void mem_write64(uint32_t vaddr, reg64_t val);

#endif // MEMORY_H