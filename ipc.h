#ifndef IPC_H
#define IPC_H

#include <stdint.h>

// Core 0 -> Core 1 SIO FIFO コマンド
#define CMD_VIRTIO_NOTIFY 0x00000001U
#define CMD_NET_MMIO      0x00000002U

// Wi-Fi / lwIP 用 MMIO 構造体 (物理アドレス 0x40000000)
#define NET_MMIO_PHYS_BASE 0x40000000U

#define NET_CMD_NONE    0
#define NET_CMD_CONNECT 1
#define NET_CMD_SEND    2
#define NET_CMD_RECV    3
#define NET_CMD_CLOSE   4

struct net_mmio_space {
    uint32_t command;       // [IN/OUT] 1:CONNECT, 2:SEND, 3:RECV, 4:CLOSE (0で完了)
    int32_t  status;        // [OUT] 成否ステータス (0:成功, <0:エラー, recv時はバイト数)
    int32_t  sock_id;       // [IN/OUT] ソケットID
    uint32_t port;          // [IN] ポート番号
    char     host[64];      // [IN] 接続先ホスト名またはIP
    uint64_t phys_buf_addr; // [IN] データバッファの物理アドレス
    uint32_t buf_len;       // [IN] バッファ長
};

#endif // IPC_H