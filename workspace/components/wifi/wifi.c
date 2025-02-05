/*
 * SPDX-FileCopyrightText: 2025 Shawn Hymel
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_event.h"
#include "esp_log.h"
#include "esp_private/wifi.h"
#include "esp_wifi.h"
#include "esp_wifi_netif.h"

#include "wifi_settings.h"

// Tag for debug messages
static const char *TAG = "wifi";

// Static global variables
static esp_netif_t *s_wifi_netif = NULL;
static EventGroupHandle_t s_wifi_event_group = NULL;

/*******************************************************************************
 * Private function prototypes
 */

static void on_wifi_event(void *arg, 
                          esp_event_base_t event_base, 
                          int32_t event_id, 
                          void *event_data);

static void on_ip_event(void *arg,
                        esp_event_base_t event_base,
                        int32_t event_id,
                        void *event_data);

static void wifi_start(void *esp_netif, 
                       esp_event_base_t base, 
                       int32_t event_id, 
                       void *data);

/*******************************************************************************
 * Private function definitions
 */

// Event handler: WiFi events
static void on_wifi_event(void *arg, 
                          esp_event_base_t event_base, 
                          int32_t event_id, 
                          void *event_data)
{

    // @todo
    //  - FIX THIS SHIT

    esp_err_t esp_ret;

    // Determine event type
    switch(event_id) {

        // Start WiFi
        case WIFI_EVENT_STA_START:
            if (s_wifi_netif != NULL) {
                wifi_start(s_wifi_netif, event_base, event_id, event_data);
            }
            break;

        // Stop WiFi
        case WIFI_EVENT_STA_STOP:
            if (s_wifi_netif != NULL) {
                esp_netif_action_stop(s_wifi_netif, event_base, event_id, event_data);
            }
            break;

        // Connect to WiFi
        case WIFI_EVENT_STA_CONNECTED:
            if (s_wifi_netif != NULL) {
                wifi_netif_driver_t driver = esp_netif_get_io_driver(s_wifi_netif);
                if (!esp_wifi_is_if_ready_when_started(driver)) {
                    esp_ret = esp_wifi_register_if_rxcb(driver, esp_netif_receive, s_wifi_netif);
                    if (esp_ret != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to register WiFi RX callback");
                        return;
                    }
                }
                esp_netif_action_connected(s_wifi_netif, event_base, event_id, event_data);

                // %%%TEST: IPv6
                esp_netif_create_ip6_linklocal(s_wifi_netif);
            }
            break;

        // Disconnect from WiFi
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_wifi_netif != NULL) {
                esp_netif_action_disconnected(s_wifi_netif, event_base, event_id, event_data);
            }
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            break;
    }
}

static void wifi_start(void *esp_netif, esp_event_base_t base, int32_t event_id, void *data)
{
    uint8_t mac[6];
    esp_err_t esp_ret;

    // Get a handle to the WiFi driver
    wifi_netif_driver_t driver = esp_netif_get_io_driver(esp_netif);
    if (driver == NULL) {
        ESP_LOGE(TAG, "Failed to get WiFi driver handle");
        return;
    }

    // Get the MAC address of the WiFi interface
    esp_ret = esp_wifi_get_if_mac(driver, mac);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_get_mac failed with %d", ret);
        return;
    }

    // Register interface receive callback
    if (esp_wifi_is_if_ready_when_started(driver)) {
        if ((ret = esp_wifi_register_if_rxcb(driver,  esp_netif_receive, esp_netif)) != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_register_if_rxcb for if=%p failed with %d", driver, ret);
            return;
        }
    } else {
        ESP_LOGI(TAG, "Interface not ready when started");
    }

    if ((ret = esp_wifi_internal_reg_netstack_buf_cb(esp_netif_netstack_buf_ref, esp_netif_netstack_buf_free)) != ESP_OK) {
        ESP_LOGE(TAG, "netstack cb reg failed with %d", ret);
        return;
    }
    esp_netif_set_mac(esp_netif, mac);
    esp_netif_action_start(esp_netif, base, event_id, data);
}
