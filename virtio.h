#ifndef VIRTIO_H
#define VIRTIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define VIRTIO0_BASE 0x10001000ULL
#define VIRTIO0_SIZE 0x100

// Flash ROM 上の fs.img をロード（無ければダミー）
#if __has_include("fs_img.inc")
#include "fs_img.inc"
#else
static const unsigned char fs_img[] = {0};
static const unsigned int fs_img_len = 0;
#endif

static uint32_t g_virtio_status = 0;
static uint32_t g_virtio_queue_ready = 0;
static uint32_t g_virtio_queue_sel = 0;
static uint32_t g_virtio_queue_num = 0;

static uint32_t g_queue_desc_low = 0;
static uint32_t g_driver_desc_low = 0;
static uint32_t g_device_desc_low = 0;
static uint32_t g_virtio_interrupt_status = 0;

extern uint8_t  mem_read8_phys(uint32_t paddr);
extern void     mem_write8_phys(uint32_t paddr, uint8_t val);
extern uint16_t mem_read16_phys(uint32_t paddr);
extern void     mem_write16_phys(uint32_t paddr, uint16_t val);
extern uint32_t mem_read32_phys(uint32_t paddr);
extern void     mem_write32_phys(uint32_t paddr, uint32_t val);
extern reg64_t  mem_read64_phys(uint32_t paddr);

static inline bool virtio_is_addr(uint32_t paddr) {
    return (paddr >= VIRTIO0_BASE && paddr < (VIRTIO0_BASE + VIRTIO0_SIZE));
}

static inline uint32_t virtio_read32(uint32_t paddr) {
    uint32_t offset = paddr - VIRTIO0_BASE;
    switch (offset) {
        case 0x000: return 0x74726976;          // MAGIC ("virt")
        case 0x004: return 2;                   // VERSION 2
        case 0x008: return 2;                   // DEVICE_ID (Disk)
        case 0x00c: return 0x554d4551;          // VENDOR_ID ("QEMU")
        case 0x010: return 0;                   // DEVICE_FEATURES
        case 0x034: return 8;                   // QUEUE_NUM_MAX
        case 0x044: return g_virtio_queue_ready;
        case 0x060: return g_virtio_interrupt_status;
        case 0x070: return g_virtio_status;
        default:    return 0;
    }
}

static inline void handle_queue_notify(void) {
    uint32_t avail_pa = g_driver_desc_low;
    uint32_t used_pa  = g_device_desc_low;
    uint32_t desc_pa  = g_queue_desc_low;

    if (avail_pa == 0 || used_pa == 0 || desc_pa == 0) return;

    uint16_t avail_idx = mem_read16_phys(avail_pa + 2);
    uint16_t used_idx  = mem_read16_phys(used_pa + 2);

    uint32_t ring_elem_addr = avail_pa + 4 + ((avail_idx - 1) % 8) * 2;
    uint16_t head_desc_idx  = mem_read16_phys(ring_elem_addr);

    // ディスクリプタ 0: virtio_blk_req
    uint32_t d0_addr = desc_pa + head_desc_idx * 16;
    reg64_t  req_header_paddr_reg = mem_read64_phys(d0_addr);
    uint32_t req_header_paddr = req_header_paddr_reg.lo;

    uint32_t req_type   = mem_read32_phys(req_header_paddr);
    reg64_t  sector_reg = mem_read64_phys(req_header_paddr + 8);
    uint64_t sector     = ((uint64_t)sector_reg.hi << 32) | sector_reg.lo;

    // ディスクリプタ 1: データバッファ (b->data)
    uint16_t d1_next = mem_read16_phys(d0_addr + 14);
    uint32_t d1_addr = desc_pa + d1_next * 16;
    reg64_t  data_paddr_reg = mem_read64_phys(d1_addr);
    uint32_t data_paddr = data_paddr_reg.lo;
    uint32_t data_len   = mem_read32_phys(d1_addr + 8);

    // ディスクリプタ 2: status バイト
    uint16_t d2_next = mem_read16_phys(d1_addr + 14);
    uint32_t d2_addr = desc_pa + d2_next * 16;
    reg64_t  status_paddr_reg = mem_read64_phys(d2_addr);
    uint32_t status_paddr = status_paddr_reg.lo;

    // Read要求 (req_type == 0): fs.img からデータをゲストRAMへコピー
    if (req_type == 0) {
        uint64_t img_offset = sector * 512;
        for (uint32_t i = 0; i < data_len; i++) {
            uint8_t byte_val = 0;
            if ((img_offset + i) < fs_img_len) {
                byte_val = fs_img[img_offset + i];
            }
            mem_write8_phys(data_paddr + i, byte_val);
        }
    }

    // ステータス 0 (成功) を書き込み
    mem_write8_phys(status_paddr, 0);

    // struct buf の b->disk (offset 4) をピンポイントで 0 にクリア
    mem_write32_phys(data_paddr - 84, 0);

    // VirtIO used ring の更新
    uint32_t used_elem_addr = used_pa + 4 + (used_idx % 8) * 8;
    mem_write32_phys(used_elem_addr, (uint32_t)head_desc_idx);
    mem_write32_phys(used_elem_addr + 4, data_len);

    mem_write16_phys(used_pa + 2, used_idx + 1);
    g_virtio_interrupt_status = 1;
}

static inline void virtio_write32(uint32_t paddr, uint32_t val) {
    uint32_t offset = paddr - VIRTIO0_BASE;
    switch (offset) {
        case 0x030: g_virtio_queue_sel = val; break;
        case 0x038: g_virtio_queue_num = val; break;
        case 0x044: g_virtio_queue_ready = val; break;
        case 0x050: handle_queue_notify(); break;
        case 0x064: g_virtio_interrupt_status &= ~val; break;
        case 0x070: g_virtio_status = val; break;
        case 0x080: g_queue_desc_low = val; break;
        case 0x090: g_driver_desc_low = val; break;
        case 0x0a0: g_device_desc_low = val; break;
        default: break;
    }
}

#endif