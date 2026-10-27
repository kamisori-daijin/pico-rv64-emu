#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "cpu.h"
#include "uart.h"
#include "clint.h"
#include "virtio.h"
#include "decode_c.h"
#include "mmu.h"

#define RAM_SIZE ((384 + 64) * 1024)
#define RAM_BASE 0x80000000ULL

static uint8_t guest_ram[RAM_SIZE];

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

// ============================================================================
// 1. 物理アドレス直引きメモリアクセス関数群 (_phys)
// ============================================================================

uint8_t mem_read8_phys(uint32_t paddr) {
    if (uart_is_addr(paddr))   return uart_read8(paddr);
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) return 0;
    return *host_ptr;
}

uint16_t mem_read16_phys(uint32_t paddr) {
    if (uart_is_addr(paddr))   return (uint16_t)uart_read8(paddr);
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) return 0;
    uint16_t val;
    __builtin_memcpy(&val, host_ptr, sizeof(val));
    return val;
}

uint32_t mem_read32_phys(uint32_t paddr) {
    if (uart_is_addr(paddr))   return (uint32_t)uart_read8(paddr);
    if (clint_is_addr(paddr))  return clint_read32(paddr);
    if (virtio_is_addr(paddr)) return virtio_read32(paddr);
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) return 0;
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

    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) __builtin_memcpy(host_ptr, &val, sizeof(val));
}

void mem_write64_phys(uint32_t paddr, reg64_t val) {
    mem_write32_phys(paddr, val.lo);
    mem_write32_phys(paddr + 4U, val.hi);
}

// ============================================================================
// 2. MMU (仮想アドレス Sv39) 経由の公開メモリアクセス関数群
// ============================================================================

uint8_t mem_read8(uint32_t vaddr) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_READ, &paddr)) {
        return mem_read8_phys(paddr);
    }
    return 0;
}

uint16_t mem_read16(uint32_t vaddr) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_READ, &paddr)) {
        return mem_read16_phys(paddr);
    }
    return 0;
}

uint32_t mem_read32(uint32_t vaddr) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_READ, &paddr)) {
        return mem_read32_phys(paddr);
    }
    printf("[READ FAULT] VA=0x%08X at PC=0x%08X\n", vaddr, g_cpu->pc.lo);
    fflush(stdout);
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
    }
}

void mem_write16(uint32_t vaddr, uint16_t val) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_WRITE, &paddr)) {
        mem_write16_phys(paddr, val);
    }
}

void mem_write32(uint32_t vaddr, uint32_t val) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = get_va_hi(vaddr);
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_WRITE, &paddr)) {
        mem_write32_phys(paddr, val);
        return;
    }
    printf("[WRITE FAULT] VA=0x%08X, Val=0x%08X at PC=0x%08X\n", vaddr, val, g_cpu->pc.lo);
    fflush(stdout);
}

void mem_write64(uint32_t vaddr, reg64_t val) {
    mem_write32(vaddr, val.lo);
    mem_write32(vaddr + 4U, val.hi);
}

// ============================================================================
// メイン関数 & エミュレーションループ
// ============================================================================

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    for (int i = 5; i > 0; i--) {
        printf("Waiting for serial terminal... %d seconds\n", i);
        sleep_ms(1000);
    }

    printf("\n========================================\n");
    printf(" RV64 Emulator on RP2350 (RVC + MMU + UART)\n");
    printf("========================================\n");

    RV64CPU cpu;
    memset(&cpu, 0, sizeof(RV64CPU));
    memset(guest_ram, 0, RAM_SIZE);

    cpu.priv_mode = 3; // Mモードスタート
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
        // 1000命令ごとにタイマー割り込みフラグをチェック更新
        if ((inst_count & 0x3FF) == 0) {
            clint_update_timer_interrupt(&cpu);
        }

        uint32_t vaddr = cpu.pc.lo;
        uint16_t half = mem_read16(vaddr);

        if ((half & 0x3) == 0x3) {
            if (vaddr % 2 != 0) {
                printf("[ERROR] Misaligned instruction fetch! PC = 0x%08X\n", vaddr);
                cpu.halted = true;
                break;
            }
            uint32_t inst = mem_read32(vaddr);
            execute_instruction(&cpu, inst);
        } else {
            execute_compressed_instruction(&cpu, half);
        }
        
        inst_count++;

        if (inst_count % 1000000 == 0) {
            printf("[HEARTBEAT] Executed %lluM insts | PC=0x%08X | Mode=%d (0:U,1:S,3:M)\n",
                   inst_count / 1000000, cpu.pc.lo, cpu.priv_mode);
            fflush(stdout);
        }
    }

    printf("CPU halted after executing %llu instructions.\n", inst_count);
    fflush(stdout);
    
    while (1) {
        tight_loop_contents();
    }

    return 0;
}