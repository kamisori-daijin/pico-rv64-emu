#ifndef VIRTIO_H
#define VIRTIO_H

#include <stdint.h>
#include <stdbool.h>

#define VIRTIO0_BASE 0x10001000ULL
#define VIRTIO0_SIZE 0x100

// エミュレータ側の VirtIO MMIO レジスタ保持変数
static uint32_t g_virtio_status = 0;
static uint32_t g_virtio_queue_ready = 0;
static uint32_t g_virtio_queue_sel = 0;
static uint32_t g_virtio_queue_num = 0;

static uint32_t g_queue_desc_low = 0;
static uint32_t g_queue_desc_high = 0;
static uint32_t g_driver_desc_low = 0;
static uint32_t g_driver_desc_high = 0;
static uint32_t g_device_desc_low = 0;
static uint32_t g_device_desc_high = 0;

static uint32_t g_virtio_interrupt_status = 0;

// main.c の物理メモリアクセス関数
extern uint8_t mem_read8_phys(uint32_t paddr);
extern void mem_write8_phys(uint32_t paddr, uint8_t val);
extern uint16_t mem_read16_phys(uint32_t paddr);
extern void mem_write16_phys(uint32_t paddr, uint16_t val);
extern uint32_t mem_read32_phys(uint32_t paddr);
extern void mem_write32_phys(uint32_t paddr, uint32_t val);

static inline bool virtio_is_addr(uint32_t paddr) {
    return (paddr >= VIRTIO0_BASE && paddr < (VIRTIO0_BASE + VIRTIO0_SIZE));
}

static inline uint32_t virtio_read32(uint32_t paddr) {
    uint32_t offset = paddr - VIRTIO0_BASE;
    switch (offset) {
        case 0x000: return 0x74726976;          // MAGIC ("virt")
        case 0x004: return 2;                   // VERSION (★ xv6は 2 を要求)
        case 0x008: return 2;                   // DEVICE_ID (Disk)
        case 0x00c: return 0x554d4551;          // VENDOR_ID ("QEMU")
        case 0x010: return 0;                   // DEVICE_FEATURES
        case 0x034: return 8;                   // QUEUE_NUM_MAX (NUM = 8)
        case 0x044: return g_virtio_queue_ready;// QUEUE_READY
        case 0x060: return g_virtio_interrupt_status; // INTERRUPT_STATUS
        case 0x070: return g_virtio_status;      // STATUS
        default:    return 0;
    }
}

// QUEUE_NOTIFY (0x050) 通知時にディスク転送完了をシミュレート
static inline void handle_queue_notify(void) {
    uint32_t avail_pa = g_driver_desc_low;
    uint32_t used_pa  = g_device_desc_low;
    uint32_t desc_pa  = g_queue_desc_low;

    if (avail_pa == 0 || used_pa == 0 || desc_pa == 0) return;

    // virtq_avail の idx を取得 (offset 2)
    uint16_t avail_idx = mem_read16_phys(avail_pa + 2);

    // virtq_used の idx を取得 (offset 2)
    uint16_t used_idx = mem_read16_phys(used_pa + 2);

    // ring からリクエストされた head_desc_idx を取得
    uint32_t ring_elem_addr = avail_pa + 4 + ((avail_idx - 1) % 8) * 2;
    uint16_t head_desc_idx  = mem_read16_phys(ring_elem_addr);

    // ディスクリプタ 3 (status バイト) の物理アドレスを取得
    // desc エントリサイズ = 16バイト
    uint32_t d1_addr = desc_pa + head_desc_idx * 16;
    uint16_t d1_next = mem_read16_phys(d1_addr + 14);

    uint32_t d2_addr = desc_pa + d1_next * 16;
    uint16_t d2_next = mem_read16_phys(d2_addr + 14);

    uint32_t d3_addr = desc_pa + d2_next * 16;
    uint32_t status_paddr = mem_read32_phys(d3_addr);

    // ステータス 0 (成功) をゲストメモリへ書込み
    mem_write8_phys(status_paddr, 0);

    // used ring に完了エントリを追加
    uint32_t used_elem_addr = used_pa + 4 + (used_idx % 8) * 8;
    mem_write32_phys(used_elem_addr, (uint32_t)head_desc_idx);
    mem_write32_phys(used_elem_addr + 4, 0); // len = 0

    // used->idx を更新
    mem_write16_phys(used_pa + 2, used_idx + 1);

    // 割り込みステータス更新
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
        case 0x084: g_queue_desc_high = val; break;
        case 0x090: g_driver_desc_low = val; break;
        case 0x094: g_driver_desc_high = val; break;
        case 0x0a0: g_device_desc_low = val; break;
        case 0x0a4: g_device_desc_high = val; break;
        default: break;
    }
}

#endif