#include "uart.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "../cpu/cpu.h"

#define RV_UART0_BASE 0x10000000U
#define RV_UART0_SIZE 0x100U

static int g_rx_char = -1;
static uint8_t g_uart_ier = 0;

extern uint32_t g_plic_uart_pending;
extern RV64CPU *g_cpu;

bool uart_is_addr(uint32_t paddr) {
    return (paddr >= RV_UART0_BASE && paddr < (RV_UART0_BASE + RV_UART0_SIZE));
}

void uart_check_input(void) {
    if (g_rx_char == -1) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            if (c == '\r') c = '\n';
            g_rx_char = c;
            
            g_plic_uart_pending = 1;
            if (g_cpu) {
                // ★ 修正: アトミック演算
                __atomic_fetch_or(&g_cpu->mip.lo, (1U << 9), __ATOMIC_RELAXED);
            }
        }
    }
}

uint8_t uart_read8(uint32_t paddr) {
    uint32_t offset = paddr - RV_UART0_BASE;
    if (offset == 0) {
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
    if (offset == 1) return g_uart_ier;
    if (offset == 2) {
        if (g_rx_char != -1) return 0x04;
        if (g_uart_ier & 0x02) return 0x02;
        return 0x01;
    }
    if (offset == 5) {
        uint8_t lsr = 0x60;
        uart_check_input();
        if (g_rx_char != -1) lsr |= 0x01;
        return lsr;
    }
    return 0;
}

void uart_write8(uint32_t paddr, uint8_t val) {
    uint32_t offset = paddr - RV_UART0_BASE;
    if (offset == 0) {
        putchar((char)val);
        fflush(stdout);

        if (g_uart_ier & 0x02) {
            g_plic_uart_pending = 1;
            if (g_cpu) {
                // ★ 修正: アトミック演算
                __atomic_fetch_or(&g_cpu->mip.lo, (1U << 9), __ATOMIC_RELAXED);
            }
        }
    } else if (offset == 1) {
        g_uart_ier = (uint8_t)val;
        if ((g_uart_ier & 0x02) && g_cpu) {
            g_plic_uart_pending = 1;
            // ★ 修正: アトミック演算
            __atomic_fetch_or(&g_cpu->mip.lo, (1U << 9), __ATOMIC_RELAXED);
        }
    }
}