#ifndef VIRTIO_H
#define VIRTIO_H

#include <stdint.h>
#include <stdbool.h>
#include "../cpu/cpu.h"

#define VIRTIO0_BASE 0x10001000ULL
#define VIRTIO0_SIZE 0x100
#define CACHE_SECTORS 32

typedef struct {
    uint64_t sector;
    uint8_t data[512];
    bool valid;
} SectorCache;

// ★ 共有変数の extern 宣言
extern const unsigned char fs_img[];
extern const unsigned int fs_img_len;

extern uint32_t g_virtio_status;
extern uint32_t g_virtio_queue_ready;
extern uint32_t g_virtio_queue_sel;
extern uint32_t g_virtio_queue_num;

extern uint32_t g_queue_desc_low;
extern uint32_t g_driver_desc_low;
extern uint32_t g_device_desc_low;
extern uint32_t g_virtio_interrupt_status;
extern SectorCache g_sector_cache[CACHE_SECTORS];

// ★ 関数のプロトタイプ宣言（実体は virtio.c）
bool virtio_is_addr(uint32_t paddr);
uint32_t virtio_read32(uint32_t paddr);
void virtio_write32(uint32_t paddr, uint32_t val);
void handle_queue_notify(void);

#endif // VIRTIO_H