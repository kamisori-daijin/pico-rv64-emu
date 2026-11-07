#include "multicore_sub.h"
#include "ipc.h"
#include "net/net.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "devices/uart.h"
#include "devices/clint.h"
#include "devices/virtio.h"
#include "cpu/cpu.h"

extern RV64CPU *g_cpu;

void core1_main(void) {
    net_init();

    while (1) {
        while (multicore_fifo_rvalid()) {
            uint32_t cmd = multicore_fifo_pop_blocking();
            if (cmd == CMD_VIRTIO_NOTIFY) {
                handle_queue_notify();
            } else if (cmd == CMD_NET_MMIO) {
                handle_net_mmio();
            }
        }

        uart_check_input();

        if (g_cpu) {
            clint_update_timer_interrupt(g_cpu);
        }

        net_poll();
    }
}