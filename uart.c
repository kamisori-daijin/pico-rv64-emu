#include "uart.h"
#include <stdio.h>

static uint8_t uart_regs[UART_SIZE];

bool uart_is_addr(uint32_t paddr) {
    return (paddr >= UART_BASE && paddr < (UART_BASE + UART_SIZE));
}

uint8_t uart_read8(uint32_t paddr) {
    uint32_t offset = paddr - UART_BASE;
    if (offset == 5) {
        // LSR (Line Status Register): 送信可能状態 (THRE + TEMT)
        return 0x60;
    }
    if (offset == 0) {
        // RHR: 受信データ
        return 0;
    }
    return uart_regs[offset];
}

void uart_write8(uint32_t paddr, uint8_t val) {
    uint32_t offset = paddr - UART_BASE;
    if (offset == 0) {
        // THR (Transmit Holding Register): 実機シリアル（USB/UART）へ出力
        putchar(val);
        fflush(stdout);
    }
    uart_regs[offset] = val;
}