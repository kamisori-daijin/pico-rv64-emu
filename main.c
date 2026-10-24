#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "cpu.h"

// ゲスト物理メモリのサイズ (例: 64MB)
#define RAM_SIZE (64 * 1024 * 1024)
#define RAM_BASE 0x80000000ULL

static uint8_t guest_ram[RAM_SIZE];

// kernel.bin.inc のインクルード (xxd -i 等で生成されたバイナリ配列)
#include "kernel.bin.inc"

// --- 物理アドレスからホストのゲストRAMポインタへの変換 ---
static inline uint8_t* paddr_to_host(uint32_t paddr) {
    if (paddr < RAM_BASE || paddr >= (RAM_BASE + RAM_SIZE)) {
        // 範囲外アクセスのガード
        return NULL;
    }
    return &guest_ram[paddr - RAM_BASE];
}

// --- メモリプリミティブ（32bitベース ＆ 64bit 2回分割） ---

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

// 32bitアクセス2回分割の mem_read64
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

// 32bitアクセス2回分割の mem_write64
void mem_write64(uint32_t paddr, reg64_t val) {
    mem_write32(paddr, val.lo);
    mem_write32(paddr + 4, val.hi);
}

// --- メイン関数 & エミュレーションループ ---

int main(void) {
    // 1. Pico SDK の初期化（USB/UART経由の printf 等を有効化）
    stdio_init_all();

    // USB経由のシリアルポート接続を少し待つ場合
    sleep_ms(2000);

    printf("\n========================================\n");
    printf(" RV64 Emulator on RP2350 (Cortex-M33)   \n");
    printf("========================================\n");

    RV64CPU cpu;
    memset(&cpu, 0, sizeof(RV64CPU));

    // ゲストメモリの初期化
    memset(guest_ram, 0, RAM_SIZE);

    // 2. カーネルバイナリをゲストメモリのベースアドレス (0x80000000) にロード
    if (sizeof(kernel_bin) > RAM_SIZE) {
        printf("Error: Kernel binary is too large for guest RAM! (%u bytes)\n", (unsigned int)sizeof(kernel_bin));
        return -1;
    }
    memcpy(&guest_ram[0], kernel_bin, kernel_bin_len);
    printf("Kernel binary loaded successfully. Size: %u bytes\n", (unsigned int)kernel_bin_len);

    // 3. PCの初期値を設定 (xv6のエントリポイント: 0x80000000)
    cpu.pc.lo = (uint32_t)RAM_BASE;
    cpu.pc.hi = 0x00000000;

    // 特権モードの初期設定 (マシンモード: 3)
    cpu.priv_mode = 3; 

    printf("Starting CPU execution loop...\n");

    unsigned long long inst_count = 0;

    // 4. フェッチ・実行メインループ
    while (!cpu.halted) {
        uint32_t paddr = cpu.pc.lo; // 現時点では簡易物理アドレス直接参照
        
        // 1命令フェッチ (32bit)
        uint32_t inst = mem_read32(paddr);

        // デコード & 実行
        execute_instruction(&cpu, inst);
        
        inst_count++;

        // デバッグ用: 一定命令数ごとに進捗を出力（有効化済み）
        if (inst_count % 1000000 == 0) {
            printf("Executed %llu instructions. PC: 0x%08X_%08X\n", inst_count, cpu.pc.hi, cpu.pc.lo);
        }
    }

    printf("CPU halted after executing %llu instructions.\n", inst_count);
    
    // 停止後の無限ループ
    while (1) {
        tight_loop_contents();
    }

    return 0;
}