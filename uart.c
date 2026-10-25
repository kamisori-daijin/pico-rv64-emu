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
    
    // デバッグ用：UART領域に何か書き込まれたらPCと一緒に即座に画面に叩き出す
    printf("[UART WRITE] addr=0x%08X, val=0x%02X ('%c')\n", paddr, val, (val >= 32 && val < 127) ? val : '.');
    
    if (offset == 0) {
        putchar(val);
        fflush(stdout);
    }
    uart_regs[offset] = val;
}