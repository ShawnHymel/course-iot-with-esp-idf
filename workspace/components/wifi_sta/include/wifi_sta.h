/*
 * SPDX-FileCopyrightText: 2025 Shawn Hymel
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WIFI_STA_H
#define WIFI_STA_H

#include "esp_err.h"

/**
 * @brief Event group bits for WiFi events
 */
#define WIFI_STA_CONNECTED_BIT      BIT0
#define WIFI_STA_IPV4_OBTAINED_BIT  BIT1
#define WIFI_STA_IPV6_OBTAINED_BIT  BIT2

/**
 * @brief Initialize WiFi in station (STA) mode.
 * 
 * Set up the WiFi interface and connect to a WiFi network. You can use the
 * event group to wait for a connection and IP address assignment. 
 * 
 * Important! you must call esp_netif_init() and esp_event_loop_create_default()
 * before calling this function.
 * 
 * @param[in] event_group Event group handle for WiFi and IP events. Pass NULL
 *                        to use the existing event group.
 * 
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t wifi_sta_init(EventGroupHandle_t event_group);

/**
 * @brief Disable WiFi
 * 
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t wifi_sta_stop(void);

/**
 * @brief Attempt to reconnect WiFi
 * 
 * @return
 *  - ESP_OK on success
 *  - Other errors on failure. See esp_err.h for error codes.
 */
esp_err_t wifi_sta_reconnect(void);

// %%%TEST
esp_err_t wifi_sta_heap_check(EventGroupHandle_t event_handle);

#endif // WIFI_H