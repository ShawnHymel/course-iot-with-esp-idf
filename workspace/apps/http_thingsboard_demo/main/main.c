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
#define API_KEY "z2ahr2c62b0xcfwo1l3w"
#define POST_BUF_SIZE 100
#define CONNECTION_TIMEOUT_SEC  10
static const uint32_t sleep_time_ms = 5000;

// HTTP endpoint and API Key for ThingsBoard
#define THINGSBOARD_HOST "demo.thingsboard.io"
#define THINGSBOARD_PATH "/api/v1/" API_KEY "/telemetry"

// Tag for debug messages
static const char *TAG = "http_thingsboard_demo";

// Event handler for HTTP client
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP event error");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP connected");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP header sent");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP header: %s: %s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                ESP_LOGD(TAG, "HTTP data received. Length: %d", evt->data_len);
                printf("%.*s\n", evt->data_len, (char*)evt->data);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP finished");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP disconnected");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP redirect");
            break;
        default:
            ESP_LOGD(TAG, "Unhandled HTTP event");
            break;
    }
    return ESP_OK;
}

// Send HTTP post to ThingsBoard
esp_err_t http_post_to_thingsboard(const char* key, int val) {

    esp_err_t esp_ret = ESP_FAIL;
    esp_http_client_handle_t client = NULL;
    char post_data[POST_BUF_SIZE];

    // Prepare JSON data
    snprintf(post_data, sizeof(post_data), "{\"%s\":%d}", key, val);

    // Configure the HTTP client
    esp_http_client_config_t config = {
        .url = "http://" THINGSBOARD_HOST THINGSBOARD_PATH,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
    };

    // Initialize the HTTP client
    client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        goto cleanup;
    }

    // Set headers
    esp_ret = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (%d): Could not set HTTP header", esp_ret);
        goto cleanup;
    }

    // Set the POST data
    esp_ret = esp_http_client_set_post_field(client, post_data, strlen(post_data));
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (%d): Could not set POST field", esp_ret);
        goto cleanup;
    }

    // Perform POST request
    esp_ret = esp_http_client_perform(client);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (%d): HTTP POST request failed", esp_ret);
        goto cleanup;
    }

    // Log success
    ESP_LOGI(TAG, 
        "HTTP POST status: %d, content_length: %" PRId64,
        esp_http_client_get_status_code(client),
        esp_http_client_get_content_length(client));

cleanup:
    // Clean up resources
    if (client) {
        esp_http_client_cleanup(client);
    }

    return esp_ret;
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
    if ((esp_ret == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (esp_ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        esp_ret = nvs_flash_init();
    }
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (%d): Could not initialize NVS", esp_ret);
        abort();
    }

    // Initialize TCP/IP network interface (init once in app)
    esp_ret = esp_netif_init();
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (%d): Failed to initialize network interface", esp_ret);
        abort();
    }

    // Create default event loop that runs in the background (init once in app)
    esp_ret = esp_event_loop_create_default();
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (%d): Failed to create default event loop", esp_ret);
        abort();
    }

    // Initialize network connection
    esp_ret = network_init(network_event_group);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (%d): Failed to initialize network", esp_ret);
        abort();
    }

    // Do forever: perform HTTP POST request to ThingsBoard
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

        // Perform HTTP POST request
        esp_ret = http_post_to_thingsboard("temp", 25);
        if (esp_ret != ESP_OK) {
            ESP_LOGE(TAG, "Error: HTTP POST failed");
        }

        // Wait before trying again
        vTaskDelay(sleep_time_ms / portTICK_PERIOD_MS);
    }
}