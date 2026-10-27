#include "uart.h"
#include <stdio.h>
#include <stdbool.h>

bool uart_is_addr(uint32_t paddr) {
    return (paddr >= 0x10000000 && paddr < 0x10000100);
}

uint8_t uart_read8(uint32_t paddr) {
    uint32_t offset = paddr - 0x10000000;
    if (offset == 5) {
        // LSR (Line Status Register): Transmit hold register empty (0x20)
        return 0x20;
    }
    return 0;
}

void uart_write8(uint32_t paddr, uint8_t val) {
    uint32_t offset = paddr - 0x10000000;
    if (offset == 0) { // THR (Transmit Holding Register)
        putchar((char)val);
        fflush(stdout);
    }
}