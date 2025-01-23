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
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "wifi_settings.h"

// Server settings and URL to fetch
#define WEB_HOST "example.com"
#define WEB_PORT "80"
#define WEB_PATH "/"

// HTTP GET request
static const char *REQUEST = "GET " WEB_PATH " HTTP/1.0\r\n"
    "Host: "WEB_HOST":"WEB_PORT"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";

// Log level for this file
#define LOG_LEVEL ESP_LOG_VERBOSE

// Set IPv4 or IPv6 family (AF_INET or AF_INET6)
#define FAMILY AF_INET6

// Tag for debug messages
static const char *TAG = "http_request";

// FreeROTS event group
static EventGroupHandle_t s_wifi_event_group;

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

// Perform DNS lookup
esp_err_t dns_lookup(const char *host, 
                     const char *port,
                     const int family,
                     struct addrinfo *ip_addr)
{
    struct addrinfo *res;
    char ip_str[INET6_ADDRSTRLEN] = {0};
    int err;

    // Check for supported address families
    if ((family != AF_INET) && (family != AF_INET6) && (family != AF_UNSPEC)) {
        ESP_LOGE(TAG, "Unsupported address family");
        return ESP_ERR_INVALID_ARG;
    }

    // Set hints for DNS lookup
    struct addrinfo hints = {
        .ai_family = family,
        .ai_socktype = SOCK_STREAM
    };

    // Perform DNS lookup
    err = getaddrinfo(host, port, &hints, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed (%d) res: %p", err, res);
        return ESP_FAIL;
    }

    // Print resolved IP addresses (linked list)
    ESP_LOGI(TAG, "DNS lookup succeeded. IP addresses:");
    for (struct addrinfo *addr = res; addr != NULL; addr = addr->ai_next) {
        if (addr->ai_family == AF_INET) {
            struct in_addr *ip = &((struct sockaddr_in *)addr->ai_addr)->sin_addr;
            inet_ntop(AF_INET, ip, ip_str, INET_ADDRSTRLEN);
            ESP_LOGI(TAG, "  IPv4: %s", ip_str);
        }
        else if (addr->ai_family == AF_INET6) {
            struct in6_addr *ip = &((struct sockaddr_in6 *)addr->ai_addr)->sin6_addr;
            inet_ntop(AF_INET6, ip, ip_str, INET6_ADDRSTRLEN);
            ESP_LOGI(TAG, "  IPv6: %s", ip_str);
        }
    }

    // Copy first resolved IP address to output struct
    if (ip_addr && res) {
        memcpy(ip_addr, res, sizeof(struct addrinfo));
    }

    // Free address info
    freeaddrinfo(res);

    return ESP_OK;
}

static void http_get_task(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];

    while(1) {
        int err = getaddrinfo(WEB_HOST, WEB_PORT, &hints, &res);

        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        /* Code to print the resolved IP.

           Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);

        if (write(s, REQUEST, strlen(REQUEST)) < 0) {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... socket send success");

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... set socket receiving timeout success");

        /* Read HTTP response */
        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            for(int i = 0; i < r; i++) {
                putchar(recv_buf[i]);
            }
        } while(r > 0);

        ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d.", r, errno);
        close(s);
        for(int countdown = 10; countdown >= 0; countdown--) {
            ESP_LOGI(TAG, "%d... ", countdown);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        ESP_LOGI(TAG, "Starting again!");
    }
}

// Main app entrypoint
void app_main(void)
{
    esp_err_t ret;
    struct addrinfo ip_addr;

    // Set log levels
    esp_log_level_set("wifi", LOG_LEVEL);
    esp_log_level_set(TAG, LOG_LEVEL);

    // Welcome message (after delay to allow serial connection)
    vTaskDelay(2000 / portTICK_PERIOD_MS);
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

    // Create task to perform HTTP GET request
    // xTaskCreate(&http_get_task, "http_get_task", 4096, NULL, 5, NULL);

    // Perform DNS lookup
    ret = dns_lookup(WEB_HOST, WEB_PORT, FAMILY, &ip_addr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DNS lookup failed");
        return;
    }

    // Create task to perform HTTP GET request

    // TODO:
    // - DNS lookup function
    // - HTTP GET request function

    // Superloop: do nothing
    while (1) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        // ESP_LOGI(TAG, "Disconnecting from AP...");
        // esp_wifi_disconnect();
    }
}