#include "uart.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "cpu.h"

#define RV_UART0_BASE 0x10000000U
#define RV_UART0_SIZE 0x100U

static int g_rx_char = -1;
extern uint32_t g_plic_uart_pending;
extern RV64CPU *g_cpu;

bool uart_is_addr(uint32_t paddr) {
    return (paddr >= RV_UART0_BASE && paddr < (RV_UART0_BASE + RV_UART0_SIZE));
}

void uart_check_input(void) {
    if (g_rx_char == -1) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            if (c == '\r') c = '\n'; // CR to LF
            g_rx_char = c;
            
            // notify PLIC and CPU
            g_plic_uart_pending = 1;
            if (g_cpu) {
                g_cpu->mip.lo |= (1U << 9); // SEIP (Bit 9) 
            }
        }
    }
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
            if (c == '\r') c = '\n';
            return (uint8_t)c;
        }
        return 0;
    }
    if (offset == 5) { // LSR (Line Status Register)
        uint8_t lsr = 0x60; // Bit 5: THR Empty, Bit 6: TEMT
        uart_check_input();
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
        sleep_us(10);
    }
}