#include "multicore_sub.h"
#include "ipc.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "devices/uart.h"
#include "devices/clint.h"
#include "devices/virtio.h"
#include "cpu/cpu.h"

extern RV64CPU *g_cpu;

void core1_main(void) {
    // Pico SDK Wi-Fi 初期化処理などの記述用 (CYW43)
    // cyw43_arch_init();

    while (1) {
        // 1. Core 0 からの SIO FIFO コマンドの処理 (非同期 I/O 実行)
        if (multicore_fifo_rvalid()) {
            uint32_t cmd = multicore_fifo_pop_blocking();
            if (cmd == CMD_VIRTIO_NOTIFY) {
                // VirtIO のセクターリード/ライト処理を Core 1 でバックグラウンド実行
                handle_queue_notify();
            } else if (cmd == CMD_NET_MMIO) {
                // Wi-Fi / lwIP コマンドの処理
                // handle_net_mmio();
            }
        }

        // 2. UART コンソール入力の非同期チェック
        uart_check_input();

        // 3. CLINT タイマー割込み状態の更新
        if (g_cpu) {
            clint_update_timer_interrupt(g_cpu);
        }

        // 4. Wi-Fi / lwIP スタックのポーリング処理
        // cyw43_arch_poll();
    }
}