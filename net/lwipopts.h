#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// ベアメタル（OSなし）設定
#define NO_SYS                      1
#define LWIP_SOCKET                 0  // ★ BSD Sockets APIを無効化 (NO_SYS=1 のため)
#define LWIP_NETCONN                0  // ★ Netconn APIを無効化
#define LWIP_TIMEVAL_PRIVATE        0

// メモリ設定
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (16 * 1024)
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24

// プロトコル設定
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_DHCP                   1

// チェックサム生成設定
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_TCP            1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1

#endif // _LWIPOPTS_H