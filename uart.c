#include "uart.h"
#include <stdio.h>
#include "pico/stdlib.h"

#define RV_UART0_BASE 0x10000000U
#define RV_UART0_SIZE 0x100U

static int g_rx_char = -1;

bool uart_is_addr(uint32_t paddr) {
    return (paddr >= RV_UART0_BASE && paddr < (RV_UART0_BASE + RV_UART0_SIZE));
}

uint8_t uart_read8(uint32_t paddr) {
    uint32_t offset = paddr - RV_UART0_BASE;
    if (offset == 0) { // RBR (Receive Buffer Register)
        if (g_rx_char != -1) {
            uint8_t c = (uint8_t)g_rx_char;
            g_rx_char = -1;
            return c;
        }
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            // シリアル端末の \r (CR) を xv6 が扱う \n (LF) や CR としてそのまま渡す
            if (c == '\r') c = '\n'; 
            return (uint8_t)c;
        }
        return 0;
    }
    if (offset == 5) { // LSR (Line Status Register)
        uint8_t lsr = 0x60; // Bit 5: THR Empty, Bit 6: TEMT (送信可能)
        if (g_rx_char == -1) {
            int c = getchar_timeout_us(0);
            if (c != PICO_ERROR_TIMEOUT) {
                if (c == '\r') c = '\n';
                g_rx_char = c;
            }
        }
        if (g_rx_char != -1) {
            lsr |= 0x01; // Bit 0: Data Ready
        }
        return lsr;
    }
    return 0;
}

void uart_write8(uint32_t paddr, uint8_t val) {
    uint32_t offset = paddr - RV_UART0_BASE;
    if (offset == 0) { // THR (Transmitter Holding Register)
        putchar((char)val);
        fflush(stdout);
    }
}