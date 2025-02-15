/*
 * SPDX-FileCopyrightText: 2025 Shawn Hymel
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NETWORK_WRAPPER_H
#define NETWORK_WRAPPER_H

#include "esp_event.h"

// Set constants based on network driver selected in menuconfig
#if CONFIG_WIFI_STA_CONNECT && !CONFIG_ETHERNET_QEMU_CONNECT
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

/**
 * @brief Initialize network driver
 * 
 * Initialize the network driver (WiFi or virtual Ethernet) and connect to the 
 * network. Choose the network driver (WIFI_STA_CONNECT or 
 * ETHERNET_QEMU_CONNECT) in menuconfig.
 * 
 * @param[in] event_group Event group handle for network events
 * 
 * @return
 * - ESP_OK on success
 * - Other errors on failure. See esp_err.h for error codes
 */
esp_err_t network_init(EventGroupHandle_t event_group);

/**
 * @brief Stop network driver (WiFi or virtual Ethernet)
 * 
 * @return
 * - ESP_OK on success
 * - Other errors on failure. See esp_err.h for error codes
 */
esp_err_t network_stop(void);

/**
 * @brief Reconnect network driver (WiFi or virtual Ethernet)
 * 
 * @return
 * - ESP_OK on success
 * - Other errors on failure. See esp_err.h for error codes
 */
esp_err_t network_reconnect(void);

/**
 * @brief Wait for network connection and IP address (blocking)
 * 
 * @param[in] network_event_group Event group handle for network events
 * @param[in] timeout_sec Timeout (seconds) to wait for network connection
 * 
 * @return true if connected and IP address obtained, false otherwise
 */
bool wait_for_network(EventGroupHandle_t network_event_group, 
                      uint32_t timeout_sec);

#endif  // NETWORK_WRAPPER_H