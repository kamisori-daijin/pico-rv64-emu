#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "cpu.h"
#include "uart.h"
#include "clint.h"
#include "decode_c.h"
#include "mmu.h"

// ゲスト物理メモリのサイズ (384KB)
#define RAM_SIZE (384 * 1024)
#define RAM_BASE 0x80000000ULL

static uint8_t guest_ram[RAM_SIZE];

// フラッシュROMの .rodata に常駐するカーネル配列
#include "kernel.bin.inc"

// MMUからCPUのCSR状態等を参照するためのグローバルポインタ
RV64CPU *g_cpu = NULL;

// --- 物理アドレスからホストのゲストRAMポインタへの変換 ---
static inline uint8_t* paddr_to_host(uint32_t paddr) {
    if (paddr < RAM_BASE || paddr >= (RAM_BASE + RAM_SIZE)) {
        return NULL;
    }
    return &guest_ram[paddr - RAM_BASE];
}

// ============================================================================
// 1. 物理アドレス直引きメモリアクセス関数群 (_phys)
//    ※ MMUの Page Table Walk や Device MMIO で使用
// ============================================================================

uint8_t mem_read8_phys(uint32_t paddr) {
    if (uart_is_addr(paddr)) {
        return uart_read8(paddr);
    }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) return 0;
    return *host_ptr;
}

uint16_t mem_read16_phys(uint32_t paddr) {
    if (uart_is_addr(paddr)) {
        return (uint16_t)uart_read8(paddr);
    }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) return 0;
    uint16_t val;
    __builtin_memcpy(&val, host_ptr, sizeof(val));
    return val;
}

uint32_t mem_read32_phys(uint32_t paddr) {
    if (uart_is_addr(paddr)) {
        return (uint32_t)uart_read8(paddr);
    }
    if (clint_is_addr(paddr)) {
        return clint_read32(paddr);
    }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (!host_ptr) return 0;
    uint32_t val;
    __builtin_memcpy(&val, host_ptr, sizeof(val));
    return val;
}

reg64_t mem_read64_phys(uint32_t paddr) {
    reg64_t val;
    val.lo = mem_read32_phys(paddr);
    val.hi = mem_read32_phys(paddr + 4);
    return val;
}

void mem_write8_phys(uint32_t paddr, uint8_t val) {
    if (uart_is_addr(paddr)) {
        uart_write8(paddr, val);
        return;
    }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) *host_ptr = val;
}

void mem_write16_phys(uint32_t paddr, uint16_t val) {
    if (uart_is_addr(paddr)) {
        uart_write8(paddr, (uint8_t)(val & 0xFF));
        return;
    }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) {
        __builtin_memcpy(host_ptr, &val, sizeof(val));
    }
}

void mem_write32_phys(uint32_t paddr, uint32_t val) {
    if (uart_is_addr(paddr)) {
        uart_write8(paddr, (uint8_t)(val & 0xFF));
        return;
    }
    if (clint_is_addr(paddr)) {
        clint_write32(paddr, val);
        return;
    }
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) {
        __builtin_memcpy(host_ptr, &val, sizeof(val));
    }
}

void mem_write64_phys(uint32_t paddr, reg64_t val) {
    mem_write32_phys(paddr, val.lo);
    mem_write32_phys(paddr + 4, val.hi);
}

// ============================================================================
// 2. MMU (仮想アドレス Sv39) 経由の公開メモリアクセス関数群
//    ※ CPUの実行・ロード・ストア処理から呼び出される
// ============================================================================

uint8_t mem_read8(uint32_t vaddr) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = ((int32_t)vaddr < 0) ? 0xFFFFFFFF : 0x00000000;
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_READ, &paddr)) {
        return mem_read8_phys(paddr);
    }
    return 0;
}

uint16_t mem_read16(uint32_t vaddr) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = ((int32_t)vaddr < 0) ? 0xFFFFFFFF : 0x00000000;
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_READ, &paddr)) {
        return mem_read16_phys(paddr);
    }
    return 0;
}

uint32_t mem_read32(uint32_t vaddr) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = ((int32_t)vaddr < 0) ? 0xFFFFFFFF : 0x00000000;
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_READ, &paddr)) {
        return mem_read32_phys(paddr);
    }
    return 0;
}

reg64_t mem_read64(uint32_t vaddr) {
    reg64_t val;
    val.lo = mem_read32(vaddr);
    val.hi = mem_read32(vaddr + 4);
    return val;
}

void mem_write8(uint32_t vaddr, uint8_t val) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = ((int32_t)vaddr < 0) ? 0xFFFFFFFF : 0x00000000;
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_WRITE, &paddr)) {
        mem_write8_phys(paddr, val);
    }
}

void mem_write16(uint32_t vaddr, uint16_t val) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = ((int32_t)vaddr < 0) ? 0xFFFFFFFF : 0x00000000;
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_WRITE, &paddr)) {
        mem_write16_phys(paddr, val);
    }
}

void mem_write32(uint32_t vaddr, uint32_t val) {
    uint32_t paddr = vaddr;
    uint32_t va_hi = ((int32_t)vaddr < 0) ? 0xFFFFFFFF : 0x00000000;
    if (mmu_translate(g_cpu, vaddr, va_hi, FETCH_WRITE, &paddr)) {
        mem_write32_phys(paddr, val);
    }
}

void mem_write64(uint32_t vaddr, reg64_t val) {
    mem_write32(vaddr, val.lo);
    mem_write32(vaddr + 4, val.hi);
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

    g_cpu = &cpu; // グローバルポインタに設定

    if (kernel_bin_len > RAM_SIZE) {
        printf("Error: Kernel binary is too large for guest RAM! (%u bytes)\n", (unsigned int)kernel_bin_len);
        return -1;
    }

    // フラッシュROM上の .rodata からゲストRAMのベースへコピー
    memcpy(&guest_ram[0], kernel_bin, kernel_bin_len);
    printf("Kernel binary loaded successfully. Size: %u bytes\n", (unsigned int)kernel_bin_len);

    // PCの初期値を設定 (xv6のエントリポイント: 0x80000000)
    cpu.pc.lo = (uint32_t)RAM_BASE;
    cpu.pc.hi = 0xFFFFFFFF; // 0x80000000 を RV64 仕様通り 0xFFFFFFFF80000000 に符号拡張
    
    // スタックポインタ (sp = x2) の初期値
    cpu.x[2].lo = (uint32_t)(RAM_BASE + RAM_SIZE);
    cpu.x[2].hi = 0xFFFFFFFF;

    printf("Starting CPU execution loop with MMU support...\n");

    unsigned long long inst_count = 0;

    // フェッチ・実行メインループ
    while (!cpu.halted) {
        uint32_t vaddr = cpu.pc.lo;
        
        // MMU経由で下位ハーフワードをフェッチ
        uint16_t half = mem_read16(vaddr);

        // 最下位2ビットで命令長を判定 (11なら32bit、それ以外は16bit RVC)
        if ((half & 0x3) == 0x3) {
            // --- 32ビット標準命令 ---
            if (vaddr % 2 != 0) {
                printf("[ERROR] Misaligned instruction fetch! PC = 0x%08X\n", vaddr);
                cpu.halted = true;
                break;
            }

            uint32_t inst = mem_read32(vaddr);

            execute_instruction(&cpu, inst);

        } else {
            // --- 16ビット圧縮命令 (RVC) ---
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