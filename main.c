#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "cpu.h"
#include "uart.h"
#include "clint.h"
#include "decode_c.h"

// ゲスト物理メモリのサイズ (256KB)
#define RAM_SIZE (256 * 1024)
#define RAM_BASE 0x80000000ULL

static uint8_t guest_ram[RAM_SIZE];

// フラッシュROMの .rodata に常駐するカーネル配列
#include "kernel.bin.inc"

// --- 物理アドレスからホストのゲストRAMポインタへの変換 ---
static inline uint8_t* paddr_to_host(uint32_t paddr) {
    if (paddr < RAM_BASE || paddr >= (RAM_BASE + RAM_SIZE)) {
        return NULL;
    }
    return &guest_ram[paddr - RAM_BASE];
}

// --- メモリ読み込み (UART / CLINT MMIO フック付き) ---

uint8_t mem_read8(uint32_t paddr) {
    if (uart_is_addr(paddr)) {
        return uart_read8(paddr);
    }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) return 0;
    return *host_ptr;
}

uint16_t mem_read16(uint32_t paddr) {
    if (uart_is_addr(paddr)) {
        return (uint16_t)uart_read8(paddr);
    }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) return 0;
    return *(uint16_t *)host_ptr;
}

uint32_t mem_read32(uint32_t paddr) {
    if (uart_is_addr(paddr)) {
        return (uint32_t)uart_read8(paddr);
    }
    if (clint_is_addr(paddr)) {
        return clint_read32(paddr);
    }
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

// --- メモリ書き込み (UART / CLINT MMIO フック付き) ---

void mem_write8(uint32_t paddr, uint8_t val) {
    if (uart_is_addr(paddr)) {
        uart_write8(paddr, val);
        return;
    }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) *host_ptr = val;
}

void mem_write16(uint32_t paddr, uint16_t val) {
    if (uart_is_addr(paddr)) {
        uart_write8(paddr, (uint8_t)(val & 0xFF));
        return;
    }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) *(uint16_t *)host_ptr = val;
}

void mem_write32(uint32_t paddr, uint32_t val) {
    if (uart_is_addr(paddr)) {
        uart_write8(paddr, (uint8_t)(val & 0xFF));
        return;
    }
    if (clint_is_addr(paddr)) {
        clint_write32(paddr, val);
        return;
    }
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

    for (int i = 5; i > 0; i--) {
        printf("Waiting for serial terminal... %d seconds\n", i);
        sleep_ms(1000);
    }

    printf("\n========================================\n");
    printf(" RV64 Emulator on RP2350 (RVC + UART)\n");
    printf("========================================\n");

    RV64CPU cpu;
    memset(&cpu, 0, sizeof(RV64CPU));
    memset(guest_ram, 0, RAM_SIZE);

    if (kernel_bin_len > RAM_SIZE) {
        printf("Error: Kernel binary is too large for guest RAM! (%u bytes)\n", (unsigned int)kernel_bin_len);
        return -1;
    }

    // フラッシュROM上の .rodata からゲストRAMのベースへコピー
    memcpy(&guest_ram[0], kernel_bin, kernel_bin_len);
    printf("Kernel binary loaded successfully. Size: %u bytes\n", (unsigned int)kernel_bin_len);

    // PCの初期値を設定 (xv6のエントリポイント: 0x80000000)
    cpu.pc.lo = (uint32_t)RAM_BASE;
    cpu.pc.hi = 0x00000000;
    cpu.priv_mode = 3; 

    printf("Starting CPU execution loop with RVC support...\n");

    unsigned long long inst_count = 0;

    // フェッチ・実行メインループ
    while (!cpu.halted) {
        uint32_t paddr = cpu.pc.lo;
        
        // まず下位ハーフワード（16bit）をフェッチして圧縮命令か判定
        uint16_t half = mem_read16(paddr);

        if ((half & 0x3) == 0x3) {
            // 32ビット標準命令 (4バイト)
            uint32_t inst = mem_read32(paddr);
            
            uint32_t old_pc = cpu.pc.lo;
            execute_instruction(&cpu, inst);
            
            // 32bitデコーダ側でPCが進んでいない場合のセーフティフォールバック (+4)
            if (cpu.pc.lo == old_pc && !cpu.halted) {
                cpu.pc.lo += 4;
            }
        } else {
            // 16ビット圧縮命令 (RVC)
            execute_compressed_instruction(&cpu, half);
        }
        
        inst_count++;

        // 一定命令数ごとの進捗表示
        if (inst_count % 5000000 == 0) {
            printf("[Stats] Executed %llu insts. PC: 0x%08X_%08X\n", inst_count, cpu.pc.hi, cpu.pc.lo);
        }
    }

    printf("CPU halted after executing %llu instructions.\n", inst_count);
    
    while (1) {
        tight_loop_contents();
    }

    return 0;
}