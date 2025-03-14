/**
 * SPDX-FileCopyrightText: 2025 Shawn Hymel
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "lwip/netdb.h"

#include "network_wrapper.h"

// Settings
static const uint32_t sleep_time_ms = 5000;
#define API_KEY "z2ahr2c62b0xcfwo1l3w"

// HTTP endpoint and API Key for ThingsBoard
#define THINGSBOARD_HOST "demo.thingsboard.io"
#define THINGSBOARD_PATH "/api/v1/" API_KEY "/telemetry"

// Content to send
#define POST_DATA "{\"temp\":25}"

// Set timeouts
#define SOCKET_TIMEOUT_SEC      5   // Set socket timeout in seconds
#define RX_BUF_SIZE             64  // Set receive buffer size (bytes)
#define CONNECTION_TIMEOUT_SEC  10  // Set delay to wait for connection (sec)

// Tag for debug messages
static const char *TAG = "http_thingsboard_demo";

// Event handler for HTTP client
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
                printf("%.*s\n", evt->data_len, (char*)evt->data);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
        default:
            ESP_LOGD(TAG, "Unhandled HTTP event");
            break;
    }
    return ESP_OK;
}

// Main function to send HTTP POST request
void http_post_to_thingsboard(void) {

    // Configure the HTTP client
    esp_http_client_config_t config = {
        .url = "http://" THINGSBOARD_HOST THINGSBOARD_PATH,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
    };

    // Initialize the HTTP client
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Set headers
    esp_http_client_set_header(client, "Content-Type", "application/json");

    // Send POST request
    esp_http_client_set_post_field(client, POST_DATA, strlen(POST_DATA));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %" PRId64,
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // Clean up
    esp_http_client_cleanup(client);
}

// Main app entrypoint
void app_main(void)
{
    esp_err_t esp_ret;
    EventGroupHandle_t network_event_group;
    EventBits_t network_event_bits;

    // Initialize event group
    network_event_group = xEventGroupCreate();

    // Initialize NVS (init once in app)
    esp_ret = nvs_flash_init();
    if (esp_ret == ESP_ERR_NVS_NO_FREE_PAGES || 
        esp_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      esp_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_ret);

    // Initialize TCP/IP network interface (init once in app)
    esp_ret = esp_netif_init();
    ESP_ERROR_CHECK(esp_ret);

    // Create default event loop that runs in the background (init once in app)
    esp_ret = esp_event_loop_create_default();
    ESP_ERROR_CHECK(esp_ret);

    // Initialize network connection
    esp_ret = network_init(network_event_group);
    ESP_ERROR_CHECK(esp_ret);

    // Do forever: perform HTTP GET request
    while (1) {

        // Make sure we have a connection and IP address
        network_event_bits = xEventGroupGetBits(network_event_group);
        if (!(network_event_bits & NETWORK_CONNECTED_BIT) ||
            !((network_event_bits & NETWORK_IPV4_OBTAINED_BIT) ||
            (network_event_bits & NETWORK_IPV6_OBTAINED_BIT))) {
            ESP_LOGI(TAG, "Network connection not established yet.");
            if (!wait_for_network(network_event_group, 
                                  CONNECTION_TIMEOUT_SEC)) {
                ESP_LOGE(TAG, "Failed to connect to WiFi. Reconnecting...");
                esp_ret = network_reconnect();
                if (esp_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to reconnect WiFi (%d)", esp_ret);
                    abort();
                }
                continue;
            }
        }

        // Superloop: send post and wait
        while(1) {
            http_post_to_thingsboard();
            vTaskDelay(sleep_time_ms / portTICK_PERIOD_MS);
        }
    }
}