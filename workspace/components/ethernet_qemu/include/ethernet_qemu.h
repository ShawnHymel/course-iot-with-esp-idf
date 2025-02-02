#ifndef ETHERNET_QEMU_H
#define ETHERNET_QEMU_H

#include "esp_err.h"

bool eth_qemu_is_connected(void);
bool eth_qemu_has_ip_addr(void);
esp_err_t eth_qemu_init(void);
esp_err_t eth_qemu_stop(void);
esp_err_t eth_qemu_reconnect(void);

#endif // ETHERNET_QEMU_H