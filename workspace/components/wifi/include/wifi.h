/*
 * SPDX-FileCopyrightText: 2025 Shawn Hymel
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"

/**
 * @brief Event group bits for WiFi events
 */
#define WIFI_CONNECTED_BIT       BIT0
#define WIFI_IPV4_CONNECTED_BIT  BIT1
#define WIFI_IPV6_CONNECTED_BIT  BIT2

/**
 * @brief Initialize WiFi
 * 
 * @param[in] event_group Event group handle for WiFi and IP events
 * 
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t wifi_init(EventGroupHandle_t event_group);

/**
 * @brief Disable WiFi
 * 
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t wifi_stop(void);

/**
 * @brief Attempt to reconnect WiFi
 * 
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t wifi_reconnect(void);

#endif // WIFI_H