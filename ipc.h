#ifndef IPC_H
#define IPC_H

#include <stdint.h>

// Core 0 -> Core 1 SIO FIFO
#define CMD_VIRTIO_NOTIFY 0x00000001U
#define CMD_NET_MMIO      0x00000002U

#define NET_MMIO_PHYS_BASE 0x40000000U

#define NET_CMD_NONE    0
#define NET_CMD_CONNECT 1
#define NET_CMD_SEND    2
#define NET_CMD_RECV    3
#define NET_CMD_CLOSE   4

struct net_mmio_space {
    uint32_t command;       // [IN/OUT] 1:CONNECT, 2:SEND, 3:RECV, 4:CLOSE 
    int32_t  status;        
    int32_t  sock_id;       // [IN/OUT] SocketID
    uint32_t port;          // [IN] 
    char     host[64];      // [IN] 
    uint64_t phys_buf_addr; // [IN] 
    uint32_t buf_len;       // [IN] 
};

#endif // IPC_H