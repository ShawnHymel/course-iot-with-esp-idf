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

#include "ethernet_qemu.h"

// Server settings and URL to fetch
#define WEB_HOST "example.com"
#define WEB_PORT "80"
#define WEB_PATH "/"

// HTTP GET request
static const char *REQUEST = "GET " WEB_PATH " HTTP/1.0\r\n"
    "Host: "WEB_HOST":"WEB_PORT"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";

// Settings
#define LOG_LEVEL               ESP_LOG_VERBOSE                 // Set log level
// Set IPv4 or IPv6 family (AF_INET, AF_INET6, AF_UNSPEC)
#if CONFIG_ETHERNET_QEMU_CONNECT_IPV4
#  define WEB_FAMILY              AF_INET
#elif CONFIG_ETHERNET_QEMU_CONNECT_IPV6
#  define WEB_FAMILY              AF_INET6
#elif CONFIG_ETHERNET_QEMU_CONNECT_UNSPECIFIED
#  define WEB_FAMILY              AF_UNSPEC
#else
#  error "Please select an IP family in menuconfig"
#endif         
#define SOCKET_TIMEOUT_SEC      5                               // Set socket timeout in seconds
#define RX_BUF_SIZE             64                              // Set receive buffer size (bytes)
#define CONNECTION_TIMEOUT_SEC  5                               // Set delay to wait for connection in seconds

// Tag for debug messages
static const char *TAG = "http_request";

// Wait for Ethernet to connect
static bool wait_for_ethernet(EventGroupHandle_t network_event_group)
{
    EventBits_t network_event_bits;

    // Wait for Ethernet to connect
    ESP_LOGI(TAG, "Waiting for Ethernet to connect...");
    network_event_bits = xEventGroupWaitBits(network_event_group, 
                                             ETHERNET_QEMU_CONNECTED_BIT, 
                                             pdFALSE, 
                                             pdTRUE, 
                                             CONNECTION_TIMEOUT_SEC * 1000 / 
                                                portTICK_PERIOD_MS);
    if (network_event_bits & ETHERNET_QEMU_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to Ethernet");
    } else {
        ESP_LOGE(TAG, "Failed to connect to Ethernet");
        return false;
    }

    // Wait for IP address
    ESP_LOGI(TAG, "Waiting for IP address...");
    network_event_bits = xEventGroupWaitBits(network_event_group, 
                                             ETHERNET_QEMU_IPV4_CONNECTED_BIT | 
                                             ETHERNET_QEMU_IPV6_CONNECTED_BIT, 
                                             pdFALSE, 
                                             pdTRUE, 
                                             CONNECTION_TIMEOUT_SEC * 1000 / 
                                                portTICK_PERIOD_MS);
    if (network_event_bits & ETHERNET_QEMU_IPV4_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to IPv4 network");
    } else if (network_event_bits & ETHERNET_QEMU_IPV6_CONNECTED_BIT) {
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
    int ret;
    struct addrinfo *dns_res;
    int sock;
    char recv_buf[RX_BUF_SIZE];
    uint32_t recv_total;
    ssize_t recv_len;
    EventGroupHandle_t network_event_group;

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

    // Set log levels
    esp_log_level_set("wifi", LOG_LEVEL);
    esp_log_level_set(TAG, LOG_LEVEL);

    // Welcome message (after delay to allow serial connection)
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Starting HTTP GET request demo");

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

    // Initialize virtual Ethernet (for QEMU)
    esp_ret = eth_qemu_init(network_event_group);
    ESP_ERROR_CHECK(esp_ret);

    // Do forever: perform HTTP GET request
    while (1) {

        // Make sure Ethernet is connected and has an IP address
        if (!wait_for_ethernet(network_event_group)) {
            ESP_LOGE(TAG, "Failed to connect to Ethernet. Reconnecting...");
            esp_ret = eth_qemu_reconnect();
            if (esp_ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to reconnect Ethernet (%d)", esp_ret);
            }
            continue;
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
        vTaskDelay(5000 / portTICK_PERIOD_MS);

        // TEST - stop ethernet to test reconnect
        esp_ret = eth_qemu_stop();
        if (esp_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop Ethernet (%d)", esp_ret);
            continue;
        }
    }
}