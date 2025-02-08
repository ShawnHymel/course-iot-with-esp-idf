/*
 * SPDX-FileCopyrightText: 2025 Shawn Hymel
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "lwip/netdb.h"

#include "wifi_sta.h"

// Settings
#define LOG_LEVEL               ESP_LOG_VERBOSE                 // Set log level
// Set IPv4 or IPv6 family (AF_INET, AF_INET6, AF_UNSPEC)
#if CONFIG_WIFI_STA_CONNECT_IPV4
#  define WEB_FAMILY              AF_INET
#elif CONFIG_WIFI_STA_CONNECT_IPV6
#  define WEB_FAMILY              AF_INET6
#elif CONFIG_WIFI_STA_CONNECT_UNSPECIFIED
#  define WEB_FAMILY              AF_UNSPEC
#else
#  error "Please select an IP family in menuconfig"
#endif         
#define CONNECTION_TIMEOUT_SEC  5                               // Set delay to wait for connection in seconds

// Tag for debug messages
static const char *TAG = "http_request";

// Wait for Ethernet to connect
static bool wait_for_network(EventGroupHandle_t network_event_group)
{
    EventBits_t network_event_bits;

    // Wait for Ethernet to connect
    ESP_LOGI(TAG, "Waiting for Ethernet to connect...");
    network_event_bits = xEventGroupWaitBits(network_event_group, 
                                             WIFI_STA_CONNECTED_BIT, 
                                             pdFALSE, 
                                             pdTRUE, 
                                             CONNECTION_TIMEOUT_SEC * 1000 / 
                                                portTICK_PERIOD_MS);
    if (network_event_bits & WIFI_STA_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to Ethernet");
    } else {
        ESP_LOGE(TAG, "Failed to connect to Ethernet");
        return false;
    }

    // Wait for IP address
    ESP_LOGI(TAG, "Waiting for IP address...");
    network_event_bits = xEventGroupWaitBits(network_event_group, 
                                             WIFI_STA_IPV4_OBTAINED_BIT | 
                                             WIFI_STA_IPV6_OBTAINED_BIT, 
                                             pdFALSE, 
                                             pdTRUE, 
                                             CONNECTION_TIMEOUT_SEC * 1000 / 
                                                portTICK_PERIOD_MS);
    if (network_event_bits & WIFI_STA_IPV4_OBTAINED_BIT) {
        ESP_LOGI(TAG, "Connected to IPv4 network");
    } else if (network_event_bits & WIFI_STA_IPV6_OBTAINED_BIT) {
        ESP_LOGI(TAG, "Connected to IPv6 network");
    } else {
        ESP_LOGE(TAG, "Failed to obtain IP address");
        return false;
    }

    return true;
}

// Main app entrypoint
void app_main(void)
{
    esp_err_t esp_ret;
    EventGroupHandle_t network_event_group;

    esp_log_level_set(TAG, LOG_LEVEL);

    // Initialize event group
    network_event_group = xEventGroupCreate();

    // Initialize NVS: ESP32 WiFi driver uses NVS to store WiFi settings
    // Erase NVS partition if it's out of free space or new version
    esp_ret = nvs_flash_init();
    if (esp_ret == ESP_ERR_NVS_NO_FREE_PAGES || esp_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      esp_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_ret);

    // Initialize TCP/IP network interface (only call once in application)
    // Must be called prior to initializing Ethernet!
    esp_ret = esp_netif_init();
    ESP_ERROR_CHECK(esp_ret);

    // Create default event loop that runs in the background
    // Must be running prior to initializing Ethernet!
    esp_ret = esp_event_loop_create_default();
    ESP_ERROR_CHECK(esp_ret);

    // // Initialize virtual Ethernet (for QEMU)
    // esp_ret = wifi_sta_init(network_event_group);
    // ESP_ERROR_CHECK(esp_ret);

    while (1) {

        esp_ret = wifi_sta_heap_check(network_event_group);
        if (esp_ret != ESP_OK) {
            ESP_LOGE(TAG, "Could not do heap check");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }

        // wifi_sta_reconnect();

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}