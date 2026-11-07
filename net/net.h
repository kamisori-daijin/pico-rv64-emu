#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stdbool.h>

#ifndef WIFI_SSID
#define WIFI_SSID "DEFAULT_SSID"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "DEFAULT_PASS"
#endif

void net_init(void);
void net_poll(void);
void handle_net_mmio(void);

#endif // NET_H