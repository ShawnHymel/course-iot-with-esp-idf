/*
 * SPDX-FileCopyrightText: 2025 Shawn Hymel
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_log.h"

#include "network_wrapper.h"

// Include the correct network driver: WiFi STA xor QEMU Ethernet
#if CONFIG_WIFI_STA_CONNECT && !CONFIG_ETHERNET_QEMU_CONNECT
# include "wifi_sta.h"
#elif CONFIG_ETHERNET_QEMU_CONNECT && !CONFIG_WIFI_STA_CONNECT
# include "ethernet_qemu.h"
#else
# error Please select one (and only one) WiFi STA or QEMU Ethernet driver in menuconfig
#endif

// Tag for debug messages
static const char *TAG = "network_wrapper";

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

// Wait for network connection and IP address (blocking)
bool wait_for_network(EventGroupHandle_t network_event_group, 
                      uint32_t timeout_sec)
{
    EventBits_t network_event_bits;

    // Wait for network to connect
    ESP_LOGI(TAG, "Waiting for network to connect...");
    network_event_bits = xEventGroupWaitBits(network_event_group, 
                                             NETWORK_CONNECTED_BIT, 
                                             pdFALSE, 
                                             pdTRUE, 
                                             pdMS_TO_TICKS(timeout_sec * 1000));
    if (network_event_bits & NETWORK_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to network");
    } else {
        ESP_LOGE(TAG, "Failed to connect to network");
        return false;
    }

    // Wait for IP address
    ESP_LOGI(TAG, "Waiting for IP address...");
    network_event_bits = xEventGroupWaitBits(network_event_group, 
                                             NETWORK_IPV4_OBTAINED_BIT | 
                                             NETWORK_IPV6_OBTAINED_BIT, 
                                             pdFALSE, 
                                             pdFALSE, 
                                             pdMS_TO_TICKS(timeout_sec * 1000));
    if (network_event_bits & NETWORK_IPV4_OBTAINED_BIT) {
        ESP_LOGI(TAG, "Connected to IPv4 network");
    } else if (network_event_bits & NETWORK_IPV6_OBTAINED_BIT) {
        ESP_LOGI(TAG, "Connected to IPv6 network");
    } else {
        ESP_LOGE(TAG, "Failed to obtain IP address");
        return false;
    }

    return true;
}