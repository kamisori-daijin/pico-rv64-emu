#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stdbool.h>

#define UART_BASE 0x10000000ULL
#define UART_SIZE 0x100

bool uart_is_addr(uint32_t paddr);
uint8_t uart_read8(uint32_t paddr);
void uart_write8(uint32_t paddr, uint8_t val);

#endif