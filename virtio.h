#ifndef VIRTIO_H
#define VIRTIO_H

#include <stdint.h>
#include <stdbool.h>

#define VIRTIO0_BASE 0x10001000ULL
#define VIRTIO0_SIZE 0x100

static inline bool virtio_is_addr(uint32_t paddr) {
    return (paddr >= VIRTIO0_BASE && paddr < (VIRTIO0_BASE + VIRTIO0_SIZE));
}

static inline uint32_t virtio_read32(uint32_t paddr) {
    uint32_t offset = paddr - VIRTIO0_BASE;
    switch (offset) {
        case 0x000: return 0x74726976; // Magic value ("virt")
        case 0x004: return 0x1;        // Version 1
        case 0x008: return 0x2;        // Device ID (Block device)
        case 0x00c: return 0x554d4551; // Vendor ID ("QEMU")
        default:    return 0;
    }
}

static inline void virtio_write32(uint32_t paddr, uint32_t val) {
    // 初期化シーケンスでの書き込み（Status変更等）を受け流す
    (void)paddr;
    (void)val;
}

#endif