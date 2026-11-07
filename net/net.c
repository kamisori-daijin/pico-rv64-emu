#include "net.h"
#include "../ipc.h"
#include "../memory.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"

#define MAX_SOCKETS 4
#define RX_BUF_SIZE 2048

typedef struct {
    struct tcp_pcb *pcb;
    bool connected;
    bool connect_failed;
    uint8_t rx_buf[RX_BUF_SIZE];
    uint16_t rx_head;
    uint16_t rx_tail;
} NetSocket;

static NetSocket g_sockets[MAX_SOCKETS];

static err_t net_tcp_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    NetSocket *sock = (NetSocket *)arg;
    if (sock) {
        if (err == ERR_OK) {
            sock->connected = true;
        } else {
            sock->connect_failed = true;
        }
    }
    return ERR_OK;
}

static err_t net_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    NetSocket *sock = (NetSocket *)arg;
    if (!sock) return ERR_OK;

    if (p == NULL) {
        sock->connected = false;
        return ERR_OK;
    }

    for (struct pbuf *q = p; q != NULL; q = q->next) {
        for (uint16_t i = 0; i < q->len; i++) {
            uint16_t next_head = (sock->rx_head + 1) % RX_BUF_SIZE;
            if (next_head != sock->rx_tail) {
                sock->rx_buf[sock->rx_head] = ((uint8_t *)q->payload)[i];
                sock->rx_head = next_head;
            }
        }
    }

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void net_tcp_err(void *arg, err_t err) {
    NetSocket *sock = (NetSocket *)arg;
    if (sock) {
        sock->connected = false;
        sock->connect_failed = true;
        sock->pcb = NULL;
    }
}

void net_init(void) {
    memset(g_sockets, 0, sizeof(g_sockets));

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
            int slot = -1;
            for (int i = 0; i < MAX_SOCKETS; i++) {
                if (!g_sockets[i].pcb && !g_sockets[i].connected) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                net->status = -1;
                break;
            }

            NetSocket *sock = &g_sockets[slot];
            memset(sock, 0, sizeof(NetSocket));

            sock->pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
            if (!sock->pcb) {
                net->status = -1;
                break;
            }

            ip_addr_t remote_ip;
                        if (!ipaddr_aton(net->host, &remote_ip)) {
                            tcp_close(sock->pcb);
                            sock->pcb = NULL;
                            net->status = -1;
                            break;
                        }
            tcp_arg(sock->pcb, sock);
            tcp_recv(sock->pcb, net_tcp_recv);
            tcp_err(sock->pcb, net_tcp_err);

            err_t err = tcp_connect(sock->pcb, &remote_ip, (uint16_t)net->port, net_tcp_connected);
            if (err != ERR_OK) {
                tcp_close(sock->pcb);
                sock->pcb = NULL;
                net->status = -1;
            } else {
                net->sock_id = slot;
                net->status = 0;
            }
            break;
        }

        case NET_CMD_SEND: {
            int id = net->sock_id;
            if (id < 0 || id >= MAX_SOCKETS || !g_sockets[id].pcb || net->phys_buf_addr < RAM_BASE) {
                net->status = -1;
                break;
            }
            NetSocket *sock = &g_sockets[id];
            uint8_t *src_buf = &guest_ram[net->phys_buf_addr - RAM_BASE];

            err_t err = tcp_write(sock->pcb, src_buf, net->buf_len, TCP_WRITE_FLAG_COPY);
            if (err == ERR_OK) {
                tcp_output(sock->pcb);
                net->status = (int32_t)net->buf_len;
            } else {
                net->status = -1;
            }
            break;
        }

        case NET_CMD_RECV: {
            int id = net->sock_id;
            if (id < 0 || id >= MAX_SOCKETS || net->phys_buf_addr < RAM_BASE) {
                net->status = -1;
                break;
            }
            NetSocket *sock = &g_sockets[id];
            uint8_t *dst_buf = &guest_ram[net->phys_buf_addr - RAM_BASE];

            uint32_t copied = 0;
            while (copied < net->buf_len && sock->rx_head != sock->rx_tail) {
                dst_buf[copied++] = sock->rx_buf[sock->rx_tail];
                sock->rx_tail = (sock->rx_tail + 1) % RX_BUF_SIZE;
            }

            net->status = (int32_t)copied;
            break;
        }

        case NET_CMD_CLOSE: {
            int id = net->sock_id;
            if (id >= 0 && id < MAX_SOCKETS && g_sockets[id].pcb) {
                tcp_arg(g_sockets[id].pcb, NULL);
                tcp_close(g_sockets[id].pcb);
                memset(&g_sockets[id], 0, sizeof(NetSocket));
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