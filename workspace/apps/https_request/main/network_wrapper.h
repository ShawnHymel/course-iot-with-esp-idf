/*
 * SPDX-FileCopyrightText: 2025 Shawn Hymel
 * SPDX-License-Identifier: Apache-2.0
 */

// Include the correct network driver: WiFi STA xor QEMU Ethernet
#if CONFIG_WIFI_STA_CONNECT && !CONFIG_ETHERNET_QEMU_CONNECT
# include "wifi_sta.h"
# define NETWORK_CONNECTED_BIT      WIFI_STA_CONNECTED_BIT
# define NETWORK_IPV4_OBTAINED_BIT  WIFI_STA_IPV4_OBTAINED_BIT
# define NETWORK_IPV6_OBTAINED_BIT  WIFI_STA_IPV6_OBTAINED_BIT
# if CONFIG_WIFI_STA_CONNECT_IPV4
#  define WEB_FAMILY                AF_INET
# elif CONFIG_WIFI_STA_CONNECT_IPV6
#  define WEB_FAMILY                AF_INET6
# elif CONFIG_WIFI_STA_CONNECT_UNSPECIFIED
#  define WEB_FAMILY                AF_UNSPEC
# else
#  error Please select an IP family from WiFi STA driver config in menuconfig
# endif
#elif CONFIG_ETHERNET_QEMU_CONNECT && !CONFIG_WIFI_STA_CONNECT
# include "ethernet_qemu.h"
# define NETWORK_CONNECTED_BIT      ETHERNET_QEMU_CONNECTED_BIT
# define NETWORK_IPV4_OBTAINED_BIT  ETHERNET_QEMU_IPV4_OBTAINED_BIT
# define NETWORK_IPV6_OBTAINED_BIT  ETHERNET_QEMU_IPV6_OBTAINED_BIT
# if CONFIG_ETHERNET_QEMU_CONNECT_IPV4
#  define WEB_FAMILY                AF_INET
# elif CONFIG_ETHERNET_QEMU_CONNECT_IPV6
#  define WEB_FAMILY                AF_INET6
# elif CONFIG_ETHERNET_QEMU_CONNECT_UNSPECIFIED
#  define WEB_FAMILY                AF_UNSPEC
# else
#  error Please select an IP family from QEMU Ethernet driver config in menuconfig
# endif
#else
# error Please select one (and only one) WiFi STA or QEMU Ethernet driver in menuconfig
#endif

// Wrapper for network driver initialization
esp_err_t network_init(EventGroupHandle_t event_group)
{
    esp_err_t esp_ret;

    // Initialize network driver
#if CONFIG_WIFI_STA_CONNECT
    esp_ret = wifi_sta_init(event_group);
#elif CONFIG_ETHERNET_QEMU_CONNECT
    esp_ret = eth_qemu_init(event_group);
#else
    esp_ret = ESP_FAIL;
#endif

    return esp_ret;
}

// Wrapper for network driver deinitialization
esp_err_t network_stop(void)
{
    esp_err_t esp_ret;

    // Stop network driver
#if CONFIG_WIFI_STA_CONNECT
    esp_ret = wifi_sta_stop();
#elif CONFIG_ETHERNET_QEMU_CONNECT
    esp_ret = eth_qemu_stop();
#else
    esp_ret = ESP_FAIL;
#endif

    return esp_ret;
}

// Wrapper for network driver reconnection
esp_err_t network_reconnect(void)
{
    esp_err_t esp_ret;

    // Reconnect network driver
#if CONFIG_WIFI_STA_CONNECT
    esp_ret = wifi_sta_reconnect();
#elif CONFIG_ETHERNET_QEMU_CONNECT
    esp_ret = eth_qemu_reconnect();
#else
    esp_ret = ESP_FAIL;
#endif

    return esp_ret;
}
