#ifndef VIRTIO_H
#define VIRTIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../cpu/cpu.h"
#include "plic.h"
#include "pico/multicore.h"
#include "../ipc.h"

#define VIRTIO0_BASE 0x10001000ULL
#define VIRTIO0_SIZE 0x100

extern const unsigned char fs_img[];
extern const unsigned int fs_img_len;

// ★ VirtIO 共有変数の extern 宣言（実体は devices/virtio.c に配置）
extern uint32_t g_virtio_status;
extern uint32_t g_virtio_queue_ready;
extern uint32_t g_virtio_queue_sel;
extern uint32_t g_virtio_queue_num;

extern uint32_t g_queue_desc_low;
extern uint32_t g_driver_desc_low;
extern uint32_t g_device_desc_low;
extern uint32_t g_virtio_interrupt_status;

#define CACHE_SECTORS 64
typedef struct {
    uint64_t sector;
    uint8_t data[512];
    bool valid;
} SectorCache;

static SectorCache g_sector_cache[CACHE_SECTORS];

static inline void cache_write_sector(uint64_t sector, const uint8_t *src) {
    for (int i = 0; i < CACHE_SECTORS; i++) {
        if (g_sector_cache[i].valid && g_sector_cache[i].sector == sector) {
            memcpy(g_sector_cache[i].data, src, 512);
            return;
        }
    }
    for (int i = 0; i < CACHE_SECTORS; i++) {
        if (!g_sector_cache[i].valid) {
            g_sector_cache[i].sector = sector;
            memcpy(g_sector_cache[i].data, src, 512);
            g_sector_cache[i].valid = true;
            return;
        }
    }
    static int victim = 0;
    g_sector_cache[victim].sector = sector;
    memcpy(g_sector_cache[victim].data, src, 512);
    g_sector_cache[victim].valid = true;
    victim = (victim + 1) % CACHE_SECTORS;
}

static inline bool cache_read_sector(uint64_t sector, uint8_t *dst) {
    for (int i = 0; i < CACHE_SECTORS; i++) {
        if (g_sector_cache[i].valid && g_sector_cache[i].sector == sector) {
            memcpy(dst, g_sector_cache[i].data, 512);
            return true;
        }
    }
    return false;
}

extern RV64CPU *g_cpu;
extern uint32_t g_plic_uart_pending;
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
        case 0x000: return 0x74726976;
        case 0x004: return 2;
        case 0x008: return 2;
        case 0x00c: return 0x554d4551;
        case 0x010: return 0;
        case 0x034: return 8;
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

    if (avail_pa == 0 || used_pa == 0 || desc_pa == 0) {
        printf("[Core1 VirtIO WARN] Uninitialized Queue addresses: desc=0x%08X, avail=0x%08X, used=0x%08X\n", desc_pa, avail_pa, used_pa);
        fflush(stdout);
        return;
    }

    uint16_t avail_idx = mem_read16_phys(avail_pa + 2);
    uint16_t used_idx  = mem_read16_phys(used_pa + 2);

    if (used_idx == avail_idx) return;

    printf("[Core1 VirtIO] Handling Queue: avail_idx=%u, used_idx=%u\n", avail_idx, used_idx);
    fflush(stdout);

    while (used_idx != avail_idx) {
        uint32_t ring_elem_addr = avail_pa + 4 + (used_idx % 8) * 2;
        uint16_t head_desc_idx  = mem_read16_phys(ring_elem_addr);

        uint32_t d0_addr = desc_pa + head_desc_idx * 16;
        reg64_t  req_header_paddr_reg = mem_read64_phys(d0_addr);
        uint32_t req_header_paddr = req_header_paddr_reg.lo;

        uint32_t req_type   = mem_read32_phys(req_header_paddr);
        reg64_t  sector_reg = mem_read64_phys(req_header_paddr + 8);
        uint64_t sector     = ((uint64_t)sector_reg.hi << 32) | sector_reg.lo;

        uint16_t d1_next = mem_read16_phys(d0_addr + 14);
        uint32_t d1_addr = desc_pa + d1_next * 16;
        reg64_t  data_paddr_reg = mem_read64_phys(d1_addr);
        uint32_t data_paddr = data_paddr_reg.lo;
        uint32_t data_len   = mem_read32_phys(d1_addr + 8);

        uint16_t d2_next = mem_read16_phys(d1_addr + 14);
        uint32_t d2_addr = desc_pa + d2_next * 16;
        reg64_t  status_paddr_reg = mem_read64_phys(d2_addr);
        uint32_t status_paddr = status_paddr_reg.lo;

        if (req_type == 0) { // READ
            for (uint32_t offset = 0; offset < data_len; offset += 512) {
                uint64_t cur_sector = sector + (offset / 512);
                uint8_t sector_buf[512];

                uint64_t img_offset = cur_sector * 512;
                for (int i = 0; i < 512; i++) {
                    if ((img_offset + i) < fs_img_len) {
                        sector_buf[i] = fs_img[img_offset + i];
                    } else {
                        sector_buf[i] = 0;
                    }
                }

                cache_read_sector(cur_sector, sector_buf);

                for (int i = 0; i < 512; i++) {
                    mem_write8_phys(data_paddr + offset + i, sector_buf[i]);
                }
            }
        } else if (req_type == 1) { // WRITE
            for (uint32_t offset = 0; offset < data_len; offset += 512) {
                uint64_t cur_sector = sector + (offset / 512);
                uint8_t sector_buf[512];
                for (int i = 0; i < 512; i++) {
                    sector_buf[i] = mem_read8_phys(data_paddr + offset + i);
                }
                cache_write_sector(cur_sector, sector_buf);
            }
        }

        mem_write8_phys(status_paddr, 0);

        uint32_t used_elem_addr = used_pa + 4 + (used_idx % 8) * 8;
        mem_write32_phys(used_elem_addr, (uint32_t)head_desc_idx);
        mem_write32_phys(used_elem_addr + 4, data_len);

        used_idx++;
        mem_write16_phys(used_pa + 2, used_idx);
    }

    g_virtio_interrupt_status = 1;
    g_plic_virtio_pending = 1;

    if (g_cpu) {
        __atomic_fetch_or(&g_cpu->mip.lo, (1U << 9), __ATOMIC_RELAXED);
        printf("[Core1 VirtIO] Raised SEIP interrupt (mip.lo=0x%08X)\n", g_cpu->mip.lo);
        fflush(stdout);
    }
}

static inline void virtio_write32(uint32_t paddr, uint32_t val) {
    uint32_t offset = paddr - VIRTIO0_BASE;
    switch (offset) {
        case 0x030: g_virtio_queue_sel = val; break;
        case 0x038: g_virtio_queue_num = val; break;
        case 0x044: g_virtio_queue_ready = val; break;
        case 0x050: 
            printf("[Core0 VirtIO] Queue Notify Write -> Push FIFO\n");
            fflush(stdout);
            multicore_fifo_push_blocking(CMD_VIRTIO_NOTIFY); 
            break;
        case 0x064: 
            g_virtio_interrupt_status &= ~val;
            if (g_virtio_interrupt_status == 0) {
                g_plic_virtio_pending = 0;
                if (g_cpu && !g_plic_uart_pending) {
                    __atomic_fetch_and(&g_cpu->mip.lo, ~(1U << 9), __ATOMIC_RELAXED);
                    printf("[Core0 VirtIO] Cleared SEIP interrupt\n");
                    fflush(stdout);
                }
            }
            break;
        case 0x070: g_virtio_status = val; break;
        case 0x080: g_queue_desc_low = val; break;
        case 0x090: g_driver_desc_low = val; break;
        case 0x0a0: g_device_desc_low = val; break;
        default: break;
    }
}

#endif // VIRTIO_H