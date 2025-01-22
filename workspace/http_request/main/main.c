#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "wifi_settings.h"

// FreeROTS event group
static EventGroupHandle_t s_wifi_event_group;

// Tag for debug messages
static const char *TAG = "WiFi STA";

// Called on WiFi events
static void wifi_event_handler(void* arg, 
                               esp_event_base_t event_base,
                               int32_t event_id, 
                               void* event_data)
{
    // Try to connect to WiFi AP
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    // If disconnected, try to reconnect in a few seconds
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, 
            "WiFi disconnected. Waiting %i seconds to reconnect.", 
            CONFIG_ESP_RECONNECT_DELAY_SEC);
        vTaskDelay((CONFIG_ESP_RECONNECT_DELAY_SEC * 1000) / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Reconnecting to %s...", ESP_WIFI_SSID);
        esp_wifi_connect();

    // If connected, print IP address and set event group bit
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Initialize WiFi in station (STA) mode
void wifi_init_sta(void)
{
    esp_err_t ret;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_event_handler_instance_t event_any_id;
    esp_event_handler_instance_t event_got_ip;

    // Create event group
    s_wifi_event_group = xEventGroupCreate();

    // Initialize TCP/IP stack
    ret = esp_netif_init();
    ESP_ERROR_CHECK(ret);

    // Create default event loop
    ret = esp_event_loop_create_default();
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi station mode
    esp_netif_create_default_wifi_sta();
    ret = esp_wifi_init(&cfg);
    ESP_ERROR_CHECK(ret);

    // Register WiFi event handler
    ret = esp_event_handler_instance_register(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              &wifi_event_handler,
                                              NULL,
                                              &event_any_id);
    ESP_ERROR_CHECK(ret);

    // Register IP address event handler
    ret = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              &wifi_event_handler,
                                              NULL,
                                              &event_got_ip);
    ESP_ERROR_CHECK(ret);
        
    // Configure WiFi connection
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASSWORD,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };

    // Set WiFi configuration and start WiFi
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_ERROR_CHECK(ret);
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    ESP_ERROR_CHECK(ret);
    ret = esp_wifi_start();
    ESP_ERROR_CHECK(ret);

    // Wait for connection and IP address assignment
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID: %s", ESP_WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "Failed to connect to AP SSID: %s", ESP_WIFI_SSID);
    }
}

// Main app entrypoint
void app_main(void)
{
    esp_err_t ret;

    // Say something
    ESP_LOGI(TAG, "Starting HTTP GET request demo");

    // Initialize NVS: ESP32 WiFi driver uses NVS to store WiFi settings
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi in station mode
    wifi_init_sta();

    // Superloop: do nothing
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS); 
    }
}