/**
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

#include "network_wrapper.h"

// Settings
static const uint32_t sleep_time_ms = 5000;

// Server settings and URL to fetch
#define WEB_HOST "example.com"
#define WEB_PORT "80"
#define WEB_PATH "/"

// HTTP GET request
static const char *REQUEST = "GET " WEB_PATH " HTTP/1.0\r\n"
    "Host: "WEB_HOST":"WEB_PORT"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";

// Set timeouts
#define SOCKET_TIMEOUT_SEC      5   // Set socket timeout in seconds
#define RX_BUF_SIZE             64  // Set receive buffer size (bytes)
#define CONNECTION_TIMEOUT_SEC  10  // Set delay to wait for connection (sec)

// Tag for debug messages
static const char *TAG = "http_request";

// Main app entrypoint
void app_main(void)
{
    esp_err_t esp_ret;
    int ret;
    struct addrinfo *dns_res;
    int sock;
    char recv_buf[RX_BUF_SIZE];
    uint32_t recv_total;
    ssize_t recv_len;
    EventGroupHandle_t network_event_group;
    EventBits_t network_event_bits;

    // Hints for DNS lookup
    struct addrinfo hints = {
        .ai_family = WEB_FAMILY,
        .ai_socktype = SOCK_STREAM
    };

    // Socket timeout
    struct timeval sock_timeout = {
        .tv_sec = SOCKET_TIMEOUT_SEC,
        .tv_usec = 0
    };

    // Welcome message (after delay to allow serial connection)
    ESP_LOGI(TAG, "Starting HTTP GET request demo");

    // Initialize event group
    network_event_group = xEventGroupCreate();

    // Initialize NVS: ESP32 WiFi driver uses NVS to store WiFi settings
    // Erase NVS partition if it's out of free space or new version
    esp_ret = nvs_flash_init();
    if (esp_ret == ESP_ERR_NVS_NO_FREE_PAGES || 
        esp_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      esp_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_ret);

    // Initialize TCP/IP network interface (only call once in application)
    // Must be called prior to initializing the network driver!
    esp_ret = esp_netif_init();
    ESP_ERROR_CHECK(esp_ret);

    // Create default event loop that runs in the background
    // Must be running prior to initializing the network driver!
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

        // Perform DNS lookup
        ret = getaddrinfo(WEB_HOST, WEB_PORT, &hints, &dns_res);
        if (ret != 0 || dns_res== NULL) {
            ESP_LOGE(TAG, "DNS lookup failed (%d)", ret);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // Print resolved IP addresses (we will just use the first)
        ESP_LOGI(TAG, "DNS lookup succeeded. IP addresses:");
        for (struct addrinfo *addr = dns_res; addr != NULL; addr = addr->ai_next) {
            if (addr->ai_family == AF_INET) {
                struct in_addr *ip = &((struct sockaddr_in *)addr->ai_addr)->sin_addr;
                inet_ntop(AF_INET, ip, recv_buf, INET_ADDRSTRLEN);
                ESP_LOGI(TAG, "  IPv4: %s", recv_buf);
            } else if (addr->ai_family == AF_INET6) {
                struct in6_addr *ip = &((struct sockaddr_in6 *)addr->ai_addr)->sin6_addr;
                inet_ntop(AF_INET6, ip, recv_buf, INET6_ADDRSTRLEN);
                ESP_LOGI(TAG, "  IPv6: %s", recv_buf);
            }
        }

        // Create a socket
        sock = socket(dns_res->ai_family, dns_res->ai_socktype, dns_res->ai_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Failed to create socket (%d): %s", errno, strerror(errno));
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // Set socket send timeout
        ret = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &sock_timeout, sizeof(sock_timeout));
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to set socket send timeout (%d): %s", errno, strerror(errno));
            close(sock);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // Set socket receive timeout
        ret = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &sock_timeout, sizeof(sock_timeout));
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to set socket receive timeout (%d): %s", errno, strerror(errno));
            close(sock);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // Connect to server
        ret = connect(sock, dns_res->ai_addr, dns_res->ai_addrlen);
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to connect to server (%d): %s", errno, strerror(errno));
            close(sock);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // Delete the address info
        freeaddrinfo(dns_res);

        // Send HTTP GET request
        ESP_LOGI(TAG, "Sending HTTP GET request...");
        ret = send(sock, REQUEST, strlen(REQUEST), 0);
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to send HTTP GET request (%d): %s", errno, strerror(errno));
            close(sock);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // Print the HTTP response
        ESP_LOGI(TAG, "HTTP response:");
        recv_total = 0;
        while (1) {

            // Receive data from the socket
            recv_len = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);

            // Check for errors
            if (recv_len < 0) {
                ESP_LOGE(TAG, "Failed to receive data (%d): %s", errno, strerror(errno));
                break;
            }

            // Check for end of data
            if (recv_len == 0) {
                break;
            }

            // Null-terminate the received data and print it
            recv_buf[recv_len] = '\0';
            printf("%s", recv_buf);
            recv_total += (uint32_t)recv_len;
        }

        // Close the socket
        close(sock);

        // Wait before trying again
        vTaskDelay(sleep_time_ms / portTICK_PERIOD_MS);
    }
}