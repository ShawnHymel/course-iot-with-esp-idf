/*
 * SPDX-FileCopyrightText: 2025 Shawn Hymel
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ETHERNET_QEMU_H
#define ETHERNET_QEMU_H

#include "esp_err.h"

/**
 * @brief Event group bits for Ethernet events
 */
#define ETHERNET_QEMU_CONNECTED_BIT       BIT0
#define ETHERNET_QEMU_IPV4_CONNECTED_BIT  BIT1
#define ETHERNET_QEMU_IPV6_CONNECTED_BIT  BIT2

/**
 * @brief Initialize Ethernet for QEMU
 * 
 * @param[in] event_group Event group handle for Ethernet and IP events
 * 
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t eth_qemu_init(EventGroupHandle_t event_group);

/**
 * @brief Disable Ethernet for QEMU
 * 
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t eth_qemu_stop(void);

/**
 * @brief Attempt to reconnect Ethernet for QEMU
 * 
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t eth_qemu_reconnect(void);

#endif // ETHERNET_QEMU_H