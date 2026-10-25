#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

// 64ビットレジスタを32ビットのペアで表現
typedef struct {
    uint32_t lo; // 下位32ビット
    uint32_t hi; // 上位32ビット
} reg64_t;

typedef struct {
    reg64_t x[32];      // 64ビット汎用レジスタ (x0 は常に 0)
    reg64_t pc;         // 64ビットプログラムカウンタ
    
    // 主要CSR
    reg64_t satp;
    reg64_t sstatus;
    reg64_t sepc;
    reg64_t mepc;
    reg64_t stvec;
    reg64_t scause;
    
    uint32_t priv_mode;  // 0: User, 1: Supervisor, 3: Machine
    bool halted;         // wfi 等での停止フラグ
} RV64CPU;

// --- メモリ読み書きプリミティブ ---
uint8_t  mem_read8(uint32_t paddr);
uint16_t mem_read16(uint32_t paddr);
uint32_t mem_read32(uint32_t paddr);
reg64_t  mem_read64(uint32_t paddr);

void mem_write8(uint32_t paddr, uint8_t val);
void mem_write16(uint32_t paddr, uint16_t val);
void mem_write32(uint32_t paddr, uint32_t val);
void mem_write64(uint32_t paddr, reg64_t val);

// 命令実行メイン関数
void execute_instruction(RV64CPU *cpu, uint32_t inst);

#endif