#include "virtio.h"
#include <stdio.h>
#include <string.h>
#include "plic.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "../ipc.h"
#include "../memory.h"

uint32_t g_virtio_status = 0;
uint32_t g_virtio_queue_ready = 0;
uint32_t g_virtio_queue_sel = 0;
uint32_t g_virtio_queue_num = 0;

uint32_t g_queue_desc_low = 0;
uint32_t g_driver_desc_low = 0;
uint32_t g_device_desc_low = 0;
uint32_t g_virtio_interrupt_status = 0;

static uint8_t sector_buf[FLASH_SECTOR_SIZE];

// 初回起動時、Flash Disk 領域が未初期化なら fs_img を書き込む
void init_flash_disk_if_needed(void) {
    const uint32_t *magic = (const uint32_t *)FLASH_DISK_ADDR;
    if (*magic == 0xFFFFFFFF) {
        printf("[Flash Disk] First boot detected. Flashing fs.img to Flash ROM...\n");
        fflush(stdout);

        uint32_t erase_len = (fs_img_len + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);

        multicore_lockout_start_blocking();
        uint32_t ints = save_and_disable_interrupts();

        flash_range_erase(FLASH_DISK_OFFSET, erase_len);
        flash_range_program(FLASH_DISK_OFFSET, fs_img, erase_len);

        restore_interrupts(ints);
        multicore_lockout_end_blocking();

        printf("[Flash Disk] fs.img initial persistent image created (%lu bytes)!\n", (unsigned long)erase_len);
        fflush(stdout);
    } else {
        printf("[Flash Disk] Existing persistent storage mounted.\n");
        fflush(stdout);
    }
}

bool virtio_is_addr(uint32_t paddr) {
    return (paddr >= VIRTIO0_BASE && paddr < (VIRTIO0_BASE + VIRTIO0_SIZE));
}

uint32_t virtio_read32(uint32_t paddr) {
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

// VirtIO キュー処理
void handle_queue_notify(void) {
    uint32_t avail_pa = g_driver_desc_low;
    uint32_t used_pa  = g_device_desc_low;
    uint32_t desc_pa  = g_queue_desc_low;

    if (avail_pa == 0 || used_pa == 0 || desc_pa == 0) return;

    uint16_t avail_idx = mem_read16_phys(avail_pa + 2);
    uint16_t used_idx  = mem_read16_phys(used_pa + 2);

    if (used_idx == avail_idx) return;

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

        if (req_type == 0) { // READ (XIP経由で直接読み出し / 0ms)
            for (uint32_t offset = 0; offset < data_len; offset += 512) {
                uint64_t cur_sector = sector + (offset / 512);
                uint32_t byte_offset = (uint32_t)(cur_sector * 512);

                const uint8_t *src_flash = (const uint8_t *)(FLASH_DISK_ADDR + byte_offset);
                for (int i = 0; i < 512; i++) {
                    mem_write8_phys(data_paddr + offset + i, src_flash[i]);
                }
            }
        } else if (req_type == 1) { // WRITE (4KB RMW + Core1 Lockout 永続化)
            for (uint32_t offset = 0; offset < data_len; offset += 512) {
                uint64_t cur_sector = sector + (offset / 512);
                uint32_t byte_offset = (uint32_t)(cur_sector * 512);
                uint32_t flash_offset = FLASH_DISK_OFFSET + byte_offset;

                uint32_t sector_start_offset = flash_offset & ~(FLASH_SECTOR_SIZE - 1);
                uint32_t offset_in_sector = flash_offset & (FLASH_SECTOR_SIZE - 1);

                // 1. 4KB セクタを SRAM にキャプチャ
                memcpy(sector_buf, (const void *)(XIP_BASE + sector_start_offset), FLASH_SECTOR_SIZE);

                // 2. 512バイト分を上書き
                for (int i = 0; i < 512; i++) {
                    sector_buf[offset_in_sector + i] = mem_read8_phys(data_paddr + offset + i);
                }

                // 3. Core 1 をロックアウト停止させて Flash 書き換え
                multicore_lockout_start_blocking();
                uint32_t ints = save_and_disable_interrupts();

                flash_range_erase(sector_start_offset, FLASH_SECTOR_SIZE);
                flash_range_program(sector_start_offset, sector_buf, FLASH_SECTOR_SIZE);

                restore_interrupts(ints);
                multicore_lockout_end_blocking();
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
    }
}

void virtio_write32(uint32_t paddr, uint32_t val) {
    uint32_t offset = paddr - VIRTIO0_BASE;
    switch (offset) {
        case 0x030: g_virtio_queue_sel = val; break;
        case 0x038: g_virtio_queue_num = val; break;
        case 0x044: g_virtio_queue_ready = val; break;
        case 0x050: 
            multicore_fifo_push_blocking(CMD_VIRTIO_NOTIFY); 
            break;
        case 0x064: 
            g_virtio_interrupt_status &= ~val;
            if (g_virtio_interrupt_status == 0) {
                g_plic_virtio_pending = 0;
                if (g_cpu && !g_plic_uart_pending) {
                    __atomic_fetch_and(&g_cpu->mip.lo, ~(1U << 9), __ATOMIC_RELAXED);
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