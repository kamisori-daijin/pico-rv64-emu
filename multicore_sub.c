#include "multicore_sub.h"
#include "ipc.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "devices/uart.h"
#include "devices/clint.h"
#include "devices/virtio.h"
#include "cpu/cpu.h"

extern RV64CPU *g_cpu;

void core1_main(void) {
    printf("[Core1] Core 1 started successfully!\n");
    fflush(stdout);

    while (1) {
        while (multicore_fifo_rvalid()) {
            uint32_t cmd = multicore_fifo_pop_blocking();
            printf("[Core1] Pop FIFO command: 0x%08X\n", cmd);
            fflush(stdout);

            if (cmd == CMD_VIRTIO_NOTIFY) {
                handle_queue_notify();
            } else if (cmd == CMD_NET_MMIO) {
                // handle_net_mmio();
            }
        }

        uart_check_input();

        if (g_cpu) {
            clint_update_timer_interrupt(g_cpu);
        }
    }
}