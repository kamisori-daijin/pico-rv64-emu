#include <stdint.h>
#include <stdbool.h>

// VirtIO 共有変数の実体定義
uint32_t g_virtio_status = 0;
uint32_t g_virtio_queue_ready = 0;
uint32_t g_virtio_queue_sel = 0;
uint32_t g_virtio_queue_num = 0;

uint32_t g_queue_desc_low = 0;
uint32_t g_driver_desc_low = 0;
uint32_t g_device_desc_low = 0;
uint32_t g_virtio_interrupt_status = 0;