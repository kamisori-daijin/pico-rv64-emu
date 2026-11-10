#ifndef VIRTIO_H
#define VIRTIO_H

#include <stdint.h>
#include <stdbool.h>
#include "../cpu/cpu.h"

#define VIRTIO0_BASE 0x10001000ULL
#define VIRTIO0_SIZE 0x100

// 1MB
#define FLASH_DISK_OFFSET (2560 * 1024)
#define FLASH_DISK_ADDR   (XIP_BASE + FLASH_DISK_OFFSET)
#define FLASH_SECTOR_SIZE 4096

extern const unsigned char fs_img[];
extern unsigned int fs_img_len;

extern uint32_t g_virtio_status;
extern uint32_t g_virtio_queue_ready;
extern uint32_t g_virtio_queue_sel;
extern uint32_t g_virtio_queue_num;

extern uint32_t g_queue_desc_low;
extern uint32_t g_driver_desc_low;
extern uint32_t g_device_desc_low;
extern uint32_t g_virtio_interrupt_status;

bool virtio_is_addr(uint32_t paddr);
uint32_t virtio_read32(uint32_t paddr);
void virtio_write32(uint32_t paddr, uint32_t val);
void handle_queue_notify(void);
void init_flash_disk_if_needed(void);

#endif // VIRTIO_H