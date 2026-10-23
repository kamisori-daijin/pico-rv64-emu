#ifndef CPU_H
#define CPU_H

#include <stdint.h>

// RV64のCPU状態を共通定義
typedef struct {
    uint64_t pc;
    uint64_t x[32];
} RV64_CPU_t;

// 別ファイルにある翻訳関数をここで宣言しておく
void execute_instruction_32(RV64_CPU_t *cpu, uint32_t insn);
void execute_instruction_16(RV64_CPU_t *cpu, uint16_t insn);

#endif
