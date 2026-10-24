#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "cpu.h"

// ゲスト物理メモリのサイズ（内蔵SRAMに収まる適度なサイズ、例: 256KB）
#define RAM_SIZE (256 * 1024)
#define RAM_BASE 0x80000000ULL

static uint8_t guest_ram[RAM_SIZE];

// objcopy でリンクされたバイナリの開始・終了シンボル
extern uint8_t _binary_kernel_bin_start[];
extern uint8_t _binary_kernel_bin_end[];

// --- 物理アドレスからホストのゲストRAMポインタへの変換 ---
static inline uint8_t* paddr_to_host(uint32_t paddr) {
    if (paddr < RAM_BASE || paddr >= (RAM_BASE + RAM_SIZE)) {
        return NULL;
    }
    return &guest_ram[paddr - RAM_BASE];
}

// --- メモリプリミティブ（フラッシュ直接読み込み対応） ---
uint8_t mem_read8(uint32_t paddr) {
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) return *host_ptr;
    
    // もしRAM領域外（例: カーネルが置かれたフラッシュ領域やMMIO等）へのアクセスなら
    // 必要に応じてフラッシュから直接読む、あるいはトラップを返す
    return 0;
}

uint16_t mem_read16(uint32_t paddr) {
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) return *(uint16_t *)host_ptr;
    return 0;
}

uint32_t mem_read32(uint32_t paddr) {
    uint8_t *host_ptr = paddr_to_host(paddr);
    if (host_ptr) return *(uint32_t *)host_ptr;
    return 0;
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

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    printf("\n========================================\n");
    printf(" RV64 Emulator on RP2350 (Flash XIP Mode)\n");
    printf("========================================\n");

    RV64CPU cpu;
    memset(&cpu, 0, sizeof(RV64CPU));
    memset(guest_ram, 0, RAM_SIZE);

    // フラッシュ上にリンクされたバイナリのサイズを計算
    size_t kernel_len = (size_t)(_binary_kernel_bin_end - _binary_kernel_bin_start);
    printf("Kernel binary in Flash. Size: %u bytes\n", (unsigned int)kernel_len);

    if (kernel_len > RAM_SIZE) {
        printf("Error: Kernel binary is too large for guest RAM! (%u bytes)\n", (unsigned int)kernel_len);
        return -1;
    }

    // 起動時にフラッシュROM上（_binary_kernel_bin_start）からゲストRAMのベースへコピーする
    memcpy(&guest_ram[0], _binary_kernel_bin_start, kernel_len);
    printf("Kernel successfully copied from Flash to Guest RAM base (0x80000000).\n");

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