#ifndef MMU_H
#define MMU_H

#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"

// アクセス種別
typedef enum {
    FETCH_EXEC, // 命令フェッチ (X)
    FETCH_READ, // データ読み込み (R)
    FETCH_WRITE // データ書き込み (W)
} AccessType;

// 仮想アドレス (VA) から物理アドレス (PA) への変換関数
// 戻り値: 成功なら true, ページフォルト等で失敗なら false
bool mmu_translate(RV64CPU *cpu, uint32_t va_lo, uint32_t va_hi, AccessType access, uint32_t *pa_out);

#endif