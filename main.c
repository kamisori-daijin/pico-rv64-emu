#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "cpu.h"

// ゲスト物理メモリのサイズ (例: 64MB)
#define RAM_SIZE (64 * 1024 * 1024)
#define RAM_BASE 0x80000000ULL

static uint8_t guest_ram[RAM_SIZE];

// --- 1. まず最初に paddr_to_host を完全に定義する ---
static inline uint8_t* paddr_to_host(uint32_t paddr) {
    if (paddr < RAM_BASE || paddr >= (RAM_BASE + RAM_SIZE)) {
        return NULL;
    }
    return &guest_ram[paddr - RAM_BASE];
}

// kernel.bin.inc のインクルード
// ※ もしファイルがない場合はビルドエラーになるため、事前に xxd -i 等で用意してください
#include "kernel.bin.inc"

// --- 2. メモリプリミティブ（paddr_to_host 定義後なので安全） ---

uint8_t mem_read8(uint32_t paddr) {
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) return 0;
    return *host_ptr;
}

uint16_t mem_read16(uint32_t paddr) {
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) return 0;
    return *(uint16_t *)host_ptr;
}

uint32_t mem_read32(uint32_t paddr) {
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) return 0;
    return *(uint32_t *)host_ptr;
}

reg64_t mem_read64(uint32_t paddr) {
    reg64_t val;
    val.lo = mem_read32(paddr);
    val.hi = mem_read32(paddr + 4);
    return val;
}

void mem_write8(uint32_t paddr, uint8_t val) {
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) *host_ptr = val;
}

void mem_write16(uint32_t paddr, uint16_t val) {
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) *(uint16_t *)host_ptr = val;
}

void mem_write32(uint32_t paddr, uint32_t val) {
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) *(uint32_t *)host_ptr = val;
}

void mem_write64(uint32_t paddr, reg64_t val) {
    mem_write32(paddr, val.lo);
    mem_write32(paddr + 4, val.hi);
}

// --- メイン関数 & エミュレーションループ ---

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    printf("\n========================================\n");
    printf(" RV64 Emulator on RP2350 (Hazard3/RISC-V)\n");
    printf("========================================\n");

    RV64CPU cpu;
    memset(&cpu, 0, sizeof(RV64CPU));
    memset(guest_ram, 0, RAM_SIZE);

    // カーネルバイナリのロードチェック
    // ※ kernel_bin および kernel_bin_len は kernel.bin.inc 内で定義される必要があります
    #ifdef kernel_bin_len
    if (kernel_bin_len > RAM_SIZE) {
        printf("Error: Kernel binary is too large for guest RAM! (%u bytes)\n", (unsigned int)kernel_bin_len);
        return -1;
    }
    memcpy(&guest_ram[0], kernel_bin, kernel_bin_len);
    printf("Kernel binary loaded successfully. Size: %u bytes\n", (unsigned int)kernel_bin_len);
    #else
    printf("Warning: kernel_bin_len is not defined. Skipping binary load.\n");
    #endif

    cpu.pc.lo = (uint32_t)RAM_BASE;
    cpu.pc.hi = 0x00000000;
    cpu.priv_mode = 3; 

    printf("Starting CPU execution loop...\n");

    unsigned long long inst_count = 0;

    while (!cpu.halted) {
        uint32_t paddr = cpu.pc.lo;
        uint32_t inst = mem_read32(paddr);

        execute_instruction(&cpu, inst);
        
        inst_count++;

        if (inst_count % 1000000 == 0) {
            printf("Executed %llu instructions. PC: 0x%08X_%08X\n", inst_count, cpu.pc.hi, cpu.pc.lo);
        }
    }

    printf("CPU halted after executing %llu instructions.\n", inst_count);
    
    while (1) {
        tight_loop_contents();
    }

    return 0;
}