#include "net.h"
#include "../ipc.h"
#include "../memory.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

void net_init(void) {
    if (cyw43_arch_init()) {
        printf("[Wi-Fi] CYW43 Init Failed!\n");
        return;
    }

    cyw43_arch_enable_sta_mode();
    printf("[Wi-Fi] Connecting to %s...\n", WIFI_SSID);

    int err = cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000
    );

    if (err) {
        printf("[Wi-Fi] Connect Failed! Code: %d\n", err);
    } else {
        printf("[Wi-Fi] Connected Successfully!\n");
    }
}

void net_poll(void) {
    cyw43_arch_poll();
}

void handle_net_mmio(void) {
    struct net_mmio_space *net = (struct net_mmio_space *)&guest_ram[NET_MMIO_PHYS_BASE - RAM_BASE];
    uint32_t cmd = net->command;
    if (cmd == NET_CMD_NONE) return;

    switch (cmd) {
        case NET_CMD_CONNECT: {
            int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                net->status = -1;
            } else {
                struct sockaddr_in server_addr;
                memset(&server_addr, 0, sizeof(server_addr));
                server_addr.sin_family = AF_INET;
                server_addr.sin_port = htons((uint16_t)net->port);
                inet_pton(AF_INET, net->host, &server_addr.sin_addr);

                int flags = lwip_fcntl(sock, F_GETFL, 0);
                lwip_fcntl(sock, F_SETFL, flags | O_NONBLOCK);

                int ret = lwip_connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
                if (ret < 0 && errno != EINPROGRESS) {
                    lwip_close(sock);
                    net->status = -1;
                } else {
                    net->sock_id = sock;
                    net->status = 0;
                }
            }
            break;
        }

        case NET_CMD_SEND: {
            if (net->sock_id < 0 || net->phys_buf_addr < RAM_BASE) {
                net->status = -1;
                break;
            }
            uint8_t *src_buf = &guest_ram[net->phys_buf_addr - RAM_BASE];
            int ret = lwip_send(net->sock_id, src_buf, net->buf_len, 0);
            net->status = (ret >= 0) ? ret : -1;
            break;
        }

        case NET_CMD_RECV: {
            if (net->sock_id < 0 || net->phys_buf_addr < RAM_BASE) {
                net->status = -1;
                break;
            }
            uint8_t *dst_buf = &guest_ram[net->phys_buf_addr - RAM_BASE];
            int ret = lwip_recv(net->sock_id, dst_buf, net->buf_len, 0);
            net->status = (ret >= 0) ? ret : -1;
            break;
        }

        case NET_CMD_CLOSE: {
            if (net->sock_id >= 0) {
                lwip_close(net->sock_id);
            }
            net->status = 0;
            break;
        }

        default:
            net->status = -1;
            break;
    }

    net->command = NET_CMD_NONE;
}