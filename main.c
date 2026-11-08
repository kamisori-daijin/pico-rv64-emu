#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "cpu/cpu.h"
#include "devices/uart.h"
#include "devices/clint.h"
#include "devices/virtio.h"
#include "mmu/tlb.h"
#include "memory.h"
#include "ipc.h"
#include "pico/multicore.h"
#include "multicore_sub.h"

uint8_t guest_ram[RAM_SIZE];
RV64CPU *g_cpu = NULL;

#include "kernel.bin.inc"

int main(void) {
    set_sys_clock_khz(250000, true);

    stdio_init_all();
    sleep_ms(1000);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("\n=== RP2350 RV64 Emulator (250MHz / Multi-Core) ===\n");
    uint32_t sys_clk = clock_get_hz(clk_sys);
    printf("Actual System Clock: %lu MHz\n", sys_clk / 1000000);

    tlb_init();

    static RV64CPU cpu;
    memset(&cpu, 0, sizeof(RV64CPU));
    memset(guest_ram, 0, RAM_SIZE);

    cpu.priv_mode = 3; // Machine mode
    g_cpu = &cpu;

    if (kernel_bin_len > RAM_SIZE) {
        printf("Error: Kernel binary is too large for guest RAM!\n");
        return -1;
    }

    memcpy(&guest_ram[0], kernel_bin, kernel_bin_len);
    printf("Kernel loaded successfully (%u bytes).\n", (unsigned int)kernel_bin_len);

    cpu.pc.lo = (uint32_t)RAM_BASE; 
    cpu.pc.hi = 0x00000000;
    cpu.x[2].lo = (uint32_t)(RAM_BASE + RAM_SIZE); 
    cpu.x[2].hi = 0x00000000;

    // 初回ブート時に fs.img を Flash ROM に書き込み、マウントする
    init_flash_disk_if_needed();

    // Core 1 で CPU エミュレーション開始
    printf("[Core 0] Launching RV64 CPU loop on Core 1...\n");
    multicore_launch_core1(core1_main);

    // Core 0 メインイベントループ (I/O, 割り込み)
    printf("[Core 0] Entering I/O Event Loop...\n");
    fflush(stdout);

    while (1) {
        while (multicore_fifo_rvalid()) {
            uint32_t cmd = multicore_fifo_pop_blocking();
            if (cmd == CMD_VIRTIO_NOTIFY) {
                handle_queue_notify();
            }
        }

        // シリアル入力とタイマー割り込み更新
        uart_check_input();

        if (g_cpu) {
            clint_update_timer_interrupt(g_cpu);
        }
    }

    return 0;
}