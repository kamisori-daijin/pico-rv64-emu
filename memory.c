#include "memory.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "ipc.h"
#include "devices/uart.h"
#include "devices/clint.h"
#include "devices/plic.h"
#include "devices/virtio.h"
#include "mmu/mmu.h"

static inline uint8_t* paddr_to_host(uint32_t paddr) {
    uint32_t offset = paddr - RAM_BASE;
    if (offset < RAM_SIZE) {
        return &guest_ram[offset];
    }
    return NULL;
}

static inline uint32_t get_va_hi(uint32_t vaddr) {
    if ((vaddr & 0xF0000000U) == 0xF0000000U) {
        return 0x0000003F;
    }
    return 0x00000000;
}

static void handle_page_fault(uint32_t vaddr, uint32_t cause) {
    g_cpu->pc_host_ptr = NULL;
    g_cpu->data_host_ptr = NULL;
    g_cpu->sepc = g_cpu->pc;
    g_cpu->stval.lo = vaddr;
    g_cpu->stval.hi = 0;
    g_cpu->scause.lo = cause;
    g_cpu->scause.hi = 0;

    uint64_t sstatus_val = ((uint64_t)g_cpu->sstatus.hi << 32) | g_cpu->sstatus.lo;
    uint64_t sie = (sstatus_val >> 1) & 1;
    sstatus_val &= ~(1ULL << 8);
    if (g_cpu->priv_mode == 1) sstatus_val |= (1ULL << 8);
    sstatus_val = (sstatus_val & ~(1ULL << 5)) | (sie << 5);
    sstatus_val &= ~(1ULL << 1);
    g_cpu->sstatus.lo = (uint32_t)sstatus_val;
    g_cpu->sstatus.hi = (uint32_t)(sstatus_val >> 32);

    g_cpu->priv_mode = 1;
    g_cpu->pc.lo = g_cpu->stvec.lo & ~0x3U;
    g_cpu->pc.hi = g_cpu->stvec.hi;

    g_cpu->trap_raised = true;
}

uint8_t __not_in_flash_func(mem_read8_phys)(uint32_t paddr) {
    if (uart_is_addr(paddr)) return uart_read8(paddr);
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) {
        printf("[PHYS READ FAULT 8] PA=0x%08X\n", paddr);
        if (g_cpu) g_cpu->halted = true;
        return 0;
    }
    return *host_ptr;
}

uint16_t __not_in_flash_func(mem_read16_phys)(uint32_t paddr) {
    if (uart_is_addr(paddr)) return (uint16_t)uart_read8(paddr);
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) {
        printf("[PHYS READ FAULT 16] PA=0x%08X\n", paddr);
        if (g_cpu) g_cpu->halted = true;
        return 0;
    }
    uint16_t val;
    __builtin_memcpy(&val, host_ptr, sizeof(val));
    return val;
}

uint32_t __not_in_flash_func(mem_read32_phys)(uint32_t paddr) {
    if (uart_is_addr(paddr))   return (uint32_t)uart_read8(paddr);
    if (clint_is_addr(paddr))  return clint_read32(paddr);
    if (virtio_is_addr(paddr)) return virtio_read32(paddr);
    if (plic_is_addr(paddr))   return plic_read32(paddr);
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) {
        printf("[PHYS READ FAULT 32] PA=0x%08X\n", paddr);
        if (g_cpu) g_cpu->halted = true;
        return 0;
    }
    uint32_t val;
    __builtin_memcpy(&val, host_ptr, sizeof(val));
    return val;
}

reg64_t __not_in_flash_func(mem_read64_phys)(uint32_t paddr) {
    reg64_t val;
    val.lo = mem_read32_phys(paddr);
    val.hi = mem_read32_phys(paddr + 4U);
    return val;
}

void __not_in_flash_func(mem_write8_phys)(uint32_t paddr, uint8_t val) {
    if (uart_is_addr(paddr)) { uart_write8(paddr, val); return; }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) *host_ptr = val;
}

void __not_in_flash_func(mem_write16_phys)(uint32_t paddr, uint16_t val) {
    if (uart_is_addr(paddr)) { uart_write8(paddr, (uint8_t)(val & 0xFF)); return; }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) __builtin_memcpy(host_ptr, &val, sizeof(val));
}

void __not_in_flash_func(mem_write32_phys)(uint32_t paddr, uint32_t val) {
    if (uart_is_addr(paddr)) { uart_write8(paddr, (uint8_t)(val & 0xFF)); return; }
    if (clint_is_addr(paddr)) { clint_write32(paddr, val); return; }
    if (virtio_is_addr(paddr)) { virtio_write32(paddr, val); return; }
    if (plic_is_addr(paddr)) { plic_write32(paddr, val, g_cpu); return; }

    if (paddr == NET_MMIO_PHYS_BASE) {
        uint8_t *host_ptr = paddr_to_host(paddr);
        if (host_ptr) __builtin_memcpy(host_ptr, &val, sizeof(val));
        if (val != NET_CMD_NONE) {
            multicore_fifo_push_blocking(CMD_NET_MMIO);
        }
        return;
    }

    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) __builtin_memcpy(host_ptr, &val, sizeof(val));
}

void __not_in_flash_func(mem_write64_phys)(uint32_t paddr, reg64_t val) {
    mem_write32_phys(paddr, val.lo);
    mem_write32_phys(paddr + 4U, val.hi);
}

uint16_t __not_in_flash_func(mem_fetch16)(uint32_t vaddr) {
    uint32_t page_va = vaddr >> 12;
    uint32_t offset = vaddr & 0xFFF;

    if (__builtin_expect(g_cpu->pc_host_ptr != NULL && g_cpu->pc_page_va == page_va && offset <= 4094, 1)) {
        uint16_t val;
        __builtin_memcpy(&val, g_cpu->pc_host_ptr + offset, sizeof(val));
        return val;
    }

    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_EXEC, &paddr)) {
        if (paddr >= RAM_BASE && paddr < (RAM_BASE + RAM_SIZE)) {
            g_cpu->pc_page_va = page_va;
            g_cpu->pc_host_ptr = &guest_ram[(paddr & ~0xFFFU) - RAM_BASE];
        } else {
            g_cpu->pc_host_ptr = NULL;
        }
        return mem_read16_phys(paddr);
    }

    if (g_cpu->priv_mode == 0) { handle_page_fault(vaddr, 12); return 0; }
    printf("[FETCH FAULT 16] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo);
    fflush(stdout);
    g_cpu->halted = true;
    return 0;
}

uint32_t __not_in_flash_func(mem_fetch32)(uint32_t vaddr) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_EXEC, &paddr)) return mem_read32_phys(paddr);
    if (g_cpu->priv_mode == 0) { handle_page_fault(vaddr, 12); return 0; }
    printf("[FETCH FAULT 32] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo);
    fflush(stdout);
    g_cpu->halted = true;
    return 0;
}

uint8_t __not_in_flash_func(mem_read8)(uint32_t vaddr) {
    uint32_t page_va = vaddr >> 12;
    uint32_t offset = vaddr & 0xFFF;
    if (__builtin_expect(g_cpu->data_host_ptr != NULL && g_cpu->data_page_va == page_va, 1)) return *(g_cpu->data_host_ptr + offset);
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_READ, &paddr)) {
        if (paddr >= RAM_BASE && paddr < (RAM_BASE + RAM_SIZE)) { g_cpu->data_page_va = page_va; g_cpu->data_host_ptr = &guest_ram[(paddr & ~0xFFFU) - RAM_BASE]; } else { g_cpu->data_host_ptr = NULL; }
        return mem_read8_phys(paddr);
    }
    if (g_cpu->priv_mode == 0) { handle_page_fault(vaddr, 13); return 0; }
    printf("[READ FAULT 8] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo); fflush(stdout); g_cpu->halted = true; return 0;
}

uint16_t __not_in_flash_func(mem_read16)(uint32_t vaddr) {
    uint32_t page_va = vaddr >> 12; uint32_t offset = vaddr & 0xFFF;
    if (__builtin_expect(g_cpu->data_host_ptr != NULL && g_cpu->data_page_va == page_va && offset <= 4094, 1)) { uint16_t val; __builtin_memcpy(&val, g_cpu->data_host_ptr + offset, sizeof(val)); return val; }
    uint32_t paddr = vaddr; uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_READ, &paddr)) {
        if (paddr >= RAM_BASE && paddr < (RAM_BASE + RAM_SIZE)) { g_cpu->data_page_va = page_va; g_cpu->data_host_ptr = &guest_ram[(paddr & ~0xFFFU) - RAM_BASE]; } else { g_cpu->data_host_ptr = NULL; }
        return mem_read16_phys(paddr);
    }
    if (g_cpu->priv_mode == 0) { handle_page_fault(vaddr, 13); return 0; }
    printf("[READ FAULT 16] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo); fflush(stdout); g_cpu->halted = true; return 0;
}

uint32_t __not_in_flash_func(mem_read32)(uint32_t vaddr) {
    uint32_t page_va = vaddr >> 12; uint32_t offset = vaddr & 0xFFF;
    if (__builtin_expect(g_cpu->data_host_ptr != NULL && g_cpu->data_page_va == page_va && offset <= 4092, 1)) { uint32_t val; __builtin_memcpy(&val, g_cpu->data_host_ptr + offset, sizeof(val)); return val; }
    uint32_t paddr = vaddr; uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_READ, &paddr)) {
        if (paddr >= RAM_BASE && paddr < (RAM_BASE + RAM_SIZE)) { g_cpu->data_page_va = page_va; g_cpu->data_host_ptr = &guest_ram[(paddr & ~0xFFFU) - RAM_BASE]; } else { g_cpu->data_host_ptr = NULL; }
        return mem_read32_phys(paddr);
    }
    if (g_cpu->priv_mode == 0) { handle_page_fault(vaddr, 13); return 0; }
    printf("[READ FAULT 32] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo); fflush(stdout); g_cpu->halted = true; return 0;
}

reg64_t __not_in_flash_func(mem_read64)(uint32_t vaddr) {
    uint32_t page_va = vaddr >> 12; uint32_t offset = vaddr & 0xFFF;
    if (__builtin_expect(g_cpu->data_host_ptr != NULL && g_cpu->data_page_va == page_va && offset <= 4088, 1)) { reg64_t val; __builtin_memcpy(&val, g_cpu->data_host_ptr + offset, sizeof(val)); return val; }
    reg64_t val; val.lo = mem_read32(vaddr); val.hi = mem_read32(vaddr + 4U); return val;
}

void __not_in_flash_func(mem_write8)(uint32_t vaddr, uint8_t val) {
    uint32_t page_va = vaddr >> 12; uint32_t offset = vaddr & 0xFFF;
    if (__builtin_expect(g_cpu->data_host_ptr != NULL && g_cpu->data_page_va == page_va, 1)) { *(g_cpu->data_host_ptr + offset) = val; return; }
    uint32_t paddr = vaddr; uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_WRITE, &paddr)) {
        if (paddr >= RAM_BASE && paddr < (RAM_BASE + RAM_SIZE)) { g_cpu->data_page_va = page_va; g_cpu->data_host_ptr = &guest_ram[(paddr & ~0xFFFU) - RAM_BASE]; } else { g_cpu->data_host_ptr = NULL; }
        mem_write8_phys(paddr, val); return;
    }
    if (g_cpu->priv_mode == 0) { handle_page_fault(vaddr, 15); return; } // ★ return 0; から return; に修正
    printf("[WRITE FAULT 8] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo); fflush(stdout); g_cpu->halted = true;
}

void __not_in_flash_func(mem_write16)(uint32_t vaddr, uint16_t val) {
    uint32_t page_va = vaddr >> 12; uint32_t offset = vaddr & 0xFFF;
    if (__builtin_expect(g_cpu->data_host_ptr != NULL && g_cpu->data_page_va == page_va && offset <= 4094, 1)) { __builtin_memcpy(g_cpu->data_host_ptr + offset, &val, sizeof(val)); return; }
    uint32_t paddr = vaddr; uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_WRITE, &paddr)) {
        if (paddr >= RAM_BASE && paddr < (RAM_BASE + RAM_SIZE)) { g_cpu->data_page_va = page_va; g_cpu->data_host_ptr = &guest_ram[(paddr & ~0xFFFU) - RAM_BASE]; } else { g_cpu->data_host_ptr = NULL; }
        mem_write16_phys(paddr, val); return;
    }
    if (g_cpu->priv_mode == 0) { handle_page_fault(vaddr, 15); return; } // ★ return 0; から return; に修正
    printf("[WRITE FAULT 16] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo); fflush(stdout); g_cpu->halted = true;
}

void __not_in_flash_func(mem_write32)(uint32_t vaddr, uint32_t val) {
    uint32_t page_va = vaddr >> 12; uint32_t offset = vaddr & 0xFFF;
    if (__builtin_expect(g_cpu->data_host_ptr != NULL && g_cpu->data_page_va == page_va && offset <= 4092, 1)) { __builtin_memcpy(g_cpu->data_host_ptr + offset, &val, sizeof(val)); return; }
    uint32_t paddr = vaddr; uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_WRITE, &paddr)) {
        if (paddr >= RAM_BASE && paddr < (RAM_BASE + RAM_SIZE)) { g_cpu->data_page_va = page_va; g_cpu->data_host_ptr = &guest_ram[(paddr & ~0xFFFU) - RAM_BASE]; } else { g_cpu->data_host_ptr = NULL; }
        mem_write32_phys(paddr, val); return;
    }
    if (g_cpu->priv_mode == 0) { handle_page_fault(vaddr, 15); return; }
    printf("[WRITE FAULT 32] VA=0x%08X, Val=0x%08X at PC=0x%08X\n", vaddr, val, g_cpu->pc.lo); fflush(stdout); g_cpu->halted = true;
}

void __not_in_flash_func(mem_write64)(uint32_t vaddr, reg64_t val) {
    uint32_t page_va = vaddr >> 12; uint32_t offset = vaddr & 0xFFF;
    if (__builtin_expect(g_cpu->data_host_ptr != NULL && g_cpu->data_page_va == page_va && offset <= 4088, 1)) { __builtin_memcpy(g_cpu->data_host_ptr + offset, &val, sizeof(val)); return; }
    mem_write32(vaddr, val.lo); mem_write32(vaddr + 4U, val.hi);
}