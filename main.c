#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "cpu.h"
#include "uart.h"
#include "clint.h"
#include "plic.h"
#include "virtio.h"
#include "decode_c.h"
#include "mmu.h"

#define RAM_SIZE (416 * 1024)
#define RAM_BASE 0x80000000ULL

uint8_t guest_ram[RAM_SIZE];

#include "kernel.bin.inc"

RV64CPU *g_cpu = NULL;

static inline uint8_t* paddr_to_host(uint32_t paddr) {
    if (paddr < RAM_BASE || paddr >= (RAM_BASE + RAM_SIZE)) {
        return NULL;
    }
    return &guest_ram[paddr - RAM_BASE];
}

static inline uint32_t get_va_hi(uint32_t vaddr) {
    if ((vaddr & 0xF0000000U) == 0xF0000000U) {
        return 0x0000003F;
    }
    return 0x00000000;
}

static void handle_page_fault(uint32_t vaddr, uint32_t cause) {
    g_cpu->sepc = g_cpu->pc;
    g_cpu->stval.lo = vaddr;
    g_cpu->stval.hi = 0;
    g_cpu->scause.lo = cause; // 12: Instruction, 13: Read, 15: Store/AMO
    g_cpu->scause.hi = 0;

    uint64_t sstatus_val = ((uint64_t)g_cpu->sstatus.hi << 32) | g_cpu->sstatus.lo;
    uint64_t sie = (sstatus_val >> 1) & 1;
    sstatus_val &= ~(1ULL << 8);                              // SPP = 0
    if (g_cpu->priv_mode == 1) sstatus_val |= (1ULL << 8);    // SPP = 1
    sstatus_val = (sstatus_val & ~(1ULL << 5)) | (sie << 5); // SPIE = SIE
    sstatus_val &= ~(1ULL << 1);                              // SIE = 0
    g_cpu->sstatus.lo = (uint32_t)sstatus_val;
    g_cpu->sstatus.hi = (uint32_t)(sstatus_val >> 32);

    g_cpu->priv_mode = 1; // transfer to s mode
    g_cpu->pc.lo = g_cpu->stvec.lo & ~0x3U;
    g_cpu->pc.hi = g_cpu->stvec.hi;

    g_cpu->trap_raised = true; // make trap flag
}

// ============================================================================
// 1. Access to physical memory(_phys)
// ============================================================================

uint8_t mem_read8_phys(uint32_t paddr) {
    if (uart_is_addr(paddr))   return uart_read8(paddr);
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) {
        printf("[PHYS READ FAULT 8] PA=0x%08X\n", paddr);
        if (g_cpu) g_cpu->halted = true;
        return 0;
    }
    return *host_ptr;
}

uint16_t mem_read16_phys(uint32_t paddr) {
    if (uart_is_addr(paddr))   return (uint16_t)uart_read8(paddr);
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

uint32_t mem_read32_phys(uint32_t paddr) {
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

reg64_t mem_read64_phys(uint32_t paddr) {
    reg64_t val;
    val.lo = mem_read32_phys(paddr);
    val.hi = mem_read32_phys(paddr + 4U);
    return val;
}

void mem_write8_phys(uint32_t paddr, uint8_t val) {
    if (uart_is_addr(paddr)) { uart_write8(paddr, val); return; }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) *host_ptr = val;
}

void mem_write16_phys(uint32_t paddr, uint16_t val) {
    if (uart_is_addr(paddr)) { uart_write8(paddr, (uint8_t)(val & 0xFF)); return; }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) __builtin_memcpy(host_ptr, &val, sizeof(val));
}

void mem_write32_phys(uint32_t paddr, uint32_t val) {
    if (uart_is_addr(paddr)) { uart_write8(paddr, (uint8_t)(val & 0xFF)); return; }
    if (clint_is_addr(paddr)) { clint_write32(paddr, val); return; }
    if (virtio_is_addr(paddr)) { virtio_write32(paddr, val); return; }
    if (plic_is_addr(paddr)) { plic_write32(paddr, val, g_cpu); return; }

    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) __builtin_memcpy(host_ptr, &val, sizeof(val));
}

void mem_write64_phys(uint32_t paddr, reg64_t val) {
    mem_write32_phys(paddr, val.lo);
    mem_write32_phys(paddr + 4U, val.hi);
}

// ============================================================================
// 2. MMU (Sv39) Route public memory
// ============================================================================

uint16_t mem_fetch16(uint32_t vaddr) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_EXEC, &paddr)) {
        return mem_read16_phys(paddr);
    }
    if (g_cpu->priv_mode == 0) {
        handle_page_fault(vaddr, 12); // Instruction Page Fault
        return 0;
    }
    printf("[FETCH FAULT 16] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo);
    fflush(stdout);
    g_cpu->halted = true;
    return 0;
}

uint32_t mem_fetch32(uint32_t vaddr) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_EXEC, &paddr)) {
        return mem_read32_phys(paddr);
    }
    if (g_cpu->priv_mode == 0) {
        handle_page_fault(vaddr, 12); // Instruction Page Fault
        return 0;
    }
    printf("[FETCH FAULT 32] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo);
    fflush(stdout);
    g_cpu->halted = true;
    return 0;
}

uint8_t mem_read8(uint32_t vaddr) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_READ, &paddr)) {
        return mem_read8_phys(paddr);
    }
    if (g_cpu->priv_mode == 0) {
        handle_page_fault(vaddr, 13); // Load Page Fault
        return 0;
    }
    printf("[READ FAULT 8] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo);
    fflush(stdout);
    g_cpu->halted = true;
    return 0;
}

uint16_t mem_read16(uint32_t vaddr) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_READ, &paddr)) {
        return mem_read16_phys(paddr);
    }
    if (g_cpu->priv_mode == 0) {
        handle_page_fault(vaddr, 13);
        return 0;
    }
    printf("[READ FAULT 16] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo);
    fflush(stdout);
    g_cpu->halted = true;
    return 0;
}

uint32_t mem_read32(uint32_t vaddr) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_READ, &paddr)) {
        return mem_read32_phys(paddr);
    }
    if (g_cpu->priv_mode == 0) {
        handle_page_fault(vaddr, 13);
        return 0;
    }
    printf("[READ FAULT 32] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo);
    fflush(stdout);
    g_cpu->halted = true;
    return 0;
}

reg64_t mem_read64(uint32_t vaddr) {
    reg64_t val;
    val.lo = mem_read32(vaddr);
    val.hi = mem_read32(vaddr + 4U);
    return val;
}

void mem_write8(uint32_t vaddr, uint8_t val) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_WRITE, &paddr)) {
        mem_write8_phys(paddr, val);
        return;
    }
    if (g_cpu->priv_mode == 0) {
        handle_page_fault(vaddr, 15); // Store/AMO Page Fault
        return;
    }
    printf("[WRITE FAULT 8] VA=0x%08X at PC=0x%08X (priv=%d, ra=0x%08X, sp=0x%08X, a0=0x%08X)\n", 
           vaddr, g_cpu->pc.lo, g_cpu->priv_mode, g_cpu->x[1].lo, g_cpu->x[2].lo, g_cpu->x[10].lo);
    fflush(stdout);
    g_cpu->halted = true;
}

void mem_write16(uint32_t vaddr, uint16_t val) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_WRITE, &paddr)) {
        mem_write16_phys(paddr, val);
        return;
    }
    if (g_cpu->priv_mode == 0) {
        handle_page_fault(vaddr, 15);
        return;
    }
    printf("[WRITE FAULT 16] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo);
    fflush(stdout);
    g_cpu->halted = true;
}

void mem_write32(uint32_t vaddr, uint32_t val) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_WRITE, &paddr)) {
        mem_write32_phys(paddr, val);
        return;
    }
    if (g_cpu->priv_mode == 0) {
        handle_page_fault(vaddr, 15);
        return;
    }
    printf("[WRITE FAULT 32] VA=0x%08X, Val=0x%08X at PC=0x%08X\n", vaddr, val, g_cpu->pc.lo);
    fflush(stdout);
    g_cpu->halted = true;
}

void mem_write64(uint32_t vaddr, reg64_t val) {
    mem_write32(vaddr, val.lo);
    mem_write32(vaddr + 4U, val.hi);
}

void check_pending_interrupts(RV64CPU *cpu) {
    uint64_t sstatus_val = ((uint64_t)cpu->sstatus.hi << 32) | cpu->sstatus.lo;
    bool sie_enabled = (sstatus_val & (1ULL << 1)) != 0;

    if (cpu->priv_mode == 0 || (cpu->priv_mode == 1 && sie_enabled)) {
        uint32_t pending = cpu->mip.lo & cpu->sie.lo;

        if (pending & (1U << 9)) {
            cpu->sepc = cpu->pc;
            cpu->scause.lo = 9U;          
            cpu->scause.hi = 0x80000000U;

            uint64_t spie = (sstatus_val >> 1) & 1;
            sstatus_val &= ~(1ULL << 8); // SPP
            if (cpu->priv_mode == 1) sstatus_val |= (1ULL << 8);
            sstatus_val = (sstatus_val & ~(1ULL << 5)) | (spie << 5); // SPIE = SIE
            sstatus_val &= ~(1ULL << 1); // SIE = 0
            cpu->sstatus.lo = (uint32_t)sstatus_val;
            cpu->sstatus.hi = (uint32_t)(sstatus_val >> 32);

            cpu->priv_mode = 1;
            cpu->pc.lo = cpu->stvec.lo & ~0x3U;
            cpu->pc.hi = cpu->stvec.hi;
        }
    }
}

// ============================================================================
// Main func and emu loop
// ============================================================================

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    for (int i = 5; i > 0; i--) {
        printf("Waiting for serial terminal... %d seconds\n", i);
        sleep_ms(1000);
    }

    printf("\n========================================\n");
    printf(" RV64 Emulator on RP2350 \n");
    printf("========================================\n");

    RV64CPU cpu;
    memset(&cpu, 0, sizeof(RV64CPU));
    memset(guest_ram, 0, RAM_SIZE);

    cpu.priv_mode = 3; // M Mode start
    g_cpu = &cpu;

    if (kernel_bin_len > RAM_SIZE) {
        printf("Error: Kernel binary is too large for guest RAM! (%u bytes)\n", (unsigned int)kernel_bin_len);
        return -1;
    }

    memcpy(&guest_ram[0], kernel_bin, kernel_bin_len);
    printf("Kernel binary loaded successfully. Size: %u bytes\n", (unsigned int)kernel_bin_len);

    cpu.pc.lo = (uint32_t)RAM_BASE;
    cpu.pc.hi = 0x00000000;
    
    cpu.x[2].lo = (uint32_t)(RAM_BASE + RAM_SIZE);
    cpu.x[2].hi = 0x00000000;

    printf("Starting CPU execution loop with MMU support...\n");
    fflush(stdout);

    unsigned long long inst_count = 0;

    while (!cpu.halted) {
        check_pending_interrupts(&cpu);
        uart_check_input();

        if ((inst_count & 0x3FF) == 0) {
            clint_update_timer_interrupt(&cpu);
        }

        cpu.trap_raised = false; // Clear per one inst

        uint32_t vaddr = cpu.pc.lo;
        uint16_t half = mem_fetch16(vaddr);
        if (cpu.halted || cpu.trap_raised) continue; // Retry when trap happened

        if ((half & 0x3) == 0x3) {
            if (vaddr % 2 != 0) {
                printf("[ERROR] Misaligned instruction fetch! PC = 0x%08X\n", vaddr);
                cpu.halted = true;
                break;
            }
            uint16_t half_high = mem_fetch16(vaddr + 2);
            if (cpu.halted || cpu.trap_raised) continue; // retry when trap happened

            uint32_t inst = (uint32_t)half | ((uint32_t)half_high << 16);
            execute_instruction(&cpu, inst);
        } else {
            execute_compressed_instruction(&cpu, half);
        }
        
        inst_count++;
    }

    printf("CPU halted after executing %llu instructions.\n", inst_count);
    fflush(stdout);
    
    while (1) {
        tight_loop_contents();
    }

    return 0;
}