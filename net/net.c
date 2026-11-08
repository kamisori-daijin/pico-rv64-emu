#include "net.h"
#include "../ipc.h"
#include "../memory.h"
#include <stdio.h>
#include <string.h>

void net_init(void) {
    printf("[Wi-Fi Bypass] CYW43 initialization disabled for test.\n");
}

void net_poll(void) {
    // なにもしない
}

void handle_net_mmio(void) {
    struct net_mmio_space *net = (struct net_mmio_space *)&guest_ram[NET_MMIO_PHYS_BASE - RAM_BASE];
    net->status = -1;
    net->command = NET_CMD_NONE;
}