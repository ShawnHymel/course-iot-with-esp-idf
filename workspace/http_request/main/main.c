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

// Settings
#define LOG_LEVEL           ESP_LOG_VERBOSE                 // Set log level
#define WEB_FAMILY          AF_INET                         // Set IPv4 or IPv6 family (AF_INET or AF_INET6)
#define SOCKET_TIMEOUT_SEC  5                               // Set socket timeout in seconds
#define RECONNECT_DELAY_SEC CONFIG_ESP_RECONNECT_DELAY_SEC  // Set delay to reconnect in seconds
#define RX_BUF_SIZE         64                              // Set receive buffer size (bytes)

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
                  RECONNECT_DELAY_SEC);
        vTaskDelay((RECONNECT_DELAY_SEC * 1000) / portTICK_PERIOD_MS);
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
    if (res && ip_addr) {
        memcpy(ip_addr, res, sizeof(struct addrinfo));
    }

    // Free address info
    freeaddrinfo(res);

    return ESP_OK;
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

    // Initialize NVS: ESP32 WiFi driver uses NVS to store WiFi settings
    esp_ret = nvs_flash_init();
    if (esp_ret == ESP_ERR_NVS_NO_FREE_PAGES || esp_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      esp_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_ret);

    // Initialize WiFi in station mode
    wifi_init_sta();

    // Do forever: perform HTTP GET request
    while (1) {

        // MY DNS LOOKUP

        // // Perform DNS lookup
        // ret = dns_lookup(WEB_HOST, WEB_PORT, WEB_FAMILY, &ip_addr);
        // if (ret != ESP_OK) {
        //     ESP_LOGE(TAG, "DNS lookup failed");
        //     continue;
        // }

        // END MY DNS LOOKUP

        // // Print resolved IP address
        // if (ip_addr.ai_family == AF_INET) {
        //     struct in_addr *ip = &((struct sockaddr_in *)ip_addr.ai_addr)->sin_addr;
        //     inet_ntop(AF_INET, ip, recv_buf, INET_ADDRSTRLEN);
        //     ESP_LOGI(TAG, "Resolved IP address: %s", recv_buf);
        // }
        // else if (ip_addr.ai_family == AF_INET6) {
        //     struct in6_addr *ip = &((struct sockaddr_in6 *)ip_addr.ai_addr)->sin6_addr;
        //     inet_ntop(AF_INET6, ip, recv_buf, INET6_ADDRSTRLEN);
        //     ESP_LOGI(TAG, "Resolved IP address: %s", recv_buf);
        // }

        // TEST: Perform DNS lookup

        // Perform DNS lookup
        ret = getaddrinfo(WEB_HOST, WEB_PORT, &hints, &dns_res);
        if (ret != 0 || dns_res== NULL) {
            ESP_LOGE(TAG, "DNS lookup failed (%d)", ret);
            continue;
        }

        // Print resolved IP addresses
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

        // // Print all resolved IP addresses
        // ESP_LOGI(TAG, "DNS lookup succeeded. IP addresses:");
        // for (struct addrinfo *addr = ip_addr; addr != NULL; addr = addr->ai_next) {
        //     if (addr->ai_family == AF_INET) {
        //         struct in_addr *ip = &((struct sockaddr_in *)addr->ai_addr)->sin_addr;
        //         inet_ntop(AF_INET, ip, ip_str, INET_ADDRSTRLEN);
        //         ESP_LOGI(TAG, "  IPv4: %s", ip_str);
        //     } else if (addr->ai_family == AF_INET6) {
        //         struct in6_addr *ip = &((struct sockaddr_in6 *)addr->ai_addr)->sin6_addr;
        //         inet_ntop(AF_INET6, ip, ip_str, INET6_ADDRSTRLEN);
        //         ESP_LOGI(TAG, "  IPv6: %s", ip_str);
        //     }
        // }

        // const struct addrinfo hints = {
        //     .ai_family = AF_INET,
        //     .ai_socktype = SOCK_STREAM,
        // };
        // struct addrinfo *res;
        // int err = getaddrinfo(WEB_HOST, WEB_PORT, &hints, &res);
        // if(err != 0 || res == NULL) {
        //     ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
        //     vTaskDelay(1000 / portTICK_PERIOD_MS);
        //     continue;
        // }

        // // Copy first resolved IP address to output struct
        // if (res) {
        //     memcpy(&ip_addr, res, sizeof(struct addrinfo));
        // }

        // END TEST
        
        // // Print resolved IP addresses (linked list)
        // char ip_str[INET6_ADDRSTRLEN] = {0};
        // ESP_LOGI(TAG, "DNS lookup succeeded. IP addresses:");
        // for (struct addrinfo *addr = res; addr != NULL; addr = addr->ai_next) {
        //     if (addr->ai_family == AF_INET) {
        //         struct in_addr *ip = &((struct sockaddr_in *)addr->ai_addr)->sin_addr;
        //         inet_ntop(AF_INET, ip, ip_str, INET_ADDRSTRLEN);
        //         ESP_LOGI(TAG, "  IPv4: %s", ip_str);
        //     }
        //     else if (addr->ai_family == AF_INET6) {
        //         struct in6_addr *ip = &((struct sockaddr_in6 *)addr->ai_addr)->sin6_addr;
        //         inet_ntop(AF_INET6, ip, ip_str, INET6_ADDRSTRLEN);
        //         ESP_LOGI(TAG, "  IPv6: %s", ip_str);
        //     }
        // }

        // Print resolved IP address, family, and port
        if (dns_res->ai_family == AF_INET) {
            struct sockaddr_in *addr_in = (struct sockaddr_in *)dns_res->ai_addr;
            // addr_in->sin_port = htons(atoi(WEB_PORT));
            ESP_LOGI(TAG, "IPv4 Address: %s", inet_ntoa(addr_in->sin_addr));
            ESP_LOGI(TAG, "  Port: %d", ntohs(addr_in->sin_port));
            ESP_LOGI(TAG, "  Family: %d", dns_res->ai_family);
            ESP_LOGI(TAG, "  Socktype: %d", dns_res->ai_socktype);
            ESP_LOGI(TAG, "  Protocol: %d", dns_res->ai_protocol);
            ESP_LOGI(TAG, "  Addrlen: %lu", (uint32_t)dns_res->ai_addrlen);
        } else if (dns_res->ai_family == AF_INET6) {
            struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)dns_res->ai_addr;
            // addr_in6->sin6_port = htons(atoi(WEB_PORT));
            char ip_str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &addr_in6->sin6_addr, ip_str, sizeof(ip_str));
            ESP_LOGI(TAG, "IPv6 Address: %s", ip_str);
            ESP_LOGI(TAG, "  Port: %d", ntohs(addr_in6->sin6_port));
            ESP_LOGI(TAG, "  Family: %d", dns_res->ai_family);
            ESP_LOGI(TAG, "  Socktype: %d", dns_res->ai_socktype);
            ESP_LOGI(TAG, "  Protocol: %d", dns_res->ai_protocol);
            ESP_LOGI(TAG, "  Addrlen: %lu", (uint32_t)dns_res->ai_addrlen);
        }

        // sock = socket(res->ai_family, res->ai_socktype, 0);
        // if(sock < 0) {
        //     ESP_LOGE(TAG, "... Failed to allocate socket.");
        //     vTaskDelay(1000 / portTICK_PERIOD_MS);
        //     continue;
        // }
        // ESP_LOGI(TAG, "... allocated socket");

        // if(connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        //     ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
        //     close(sock);
        //     vTaskDelay(4000 / portTICK_PERIOD_MS);
        //     continue;
        // }

        // ESP_LOGI(TAG, "... connected");

        // if (write(sock, REQUEST, strlen(REQUEST)) < 0) {
        //     ESP_LOGE(TAG, "... socket send failed");
        //     close(sock);
        //     vTaskDelay(4000 / portTICK_PERIOD_MS);
        //     continue;
        // }
        // ESP_LOGI(TAG, "... socket send success");

        // struct timeval receiving_timeout;
        // receiving_timeout.tv_sec = 5;
        // receiving_timeout.tv_usec = 0;
        // if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
        //         sizeof(receiving_timeout)) < 0) {
        //     ESP_LOGE(TAG, "... failed to set socket receiving timeout");
        //     close(sock);
        //     vTaskDelay(4000 / portTICK_PERIOD_MS);
        //     continue;
        // }
        // ESP_LOGI(TAG, "... set socket receiving timeout success");

        // /* Read HTTP response */
        // do {
        //     bzero(recv_buf, sizeof(recv_buf));
        //     recv_len = read(sock, recv_buf, sizeof(recv_buf)-1);
        //     for(int i = 0; i < recv_len; i++) {
        //         putchar(recv_buf[i]);
        //     }
        // } while(recv_len > 0);

        // ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d.", recv_len, errno);
        // close(sock);

        // // Add port to the address
        // if (ip_addr.ai_family == AF_INET) {
        //     struct sockaddr_in *addr_in = (struct sockaddr_in *)ip_addr.ai_addr;
        //     addr_in->sin_port = htons(atoi(WEB_PORT));
        //     ESP_LOGI(TAG, "IPv4 Address: %s, Port: %d",
        //             inet_ntoa(addr_in->sin_addr), ntohs(addr_in->sin_port));
        // } else if (ip_addr.ai_family == AF_INET6) {
        //     struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)ip_addr.ai_addr;
        //     addr_in6->sin6_port = htons(atoi(WEB_PORT));
        //     char ip_str[INET6_ADDRSTRLEN];
        //     inet_ntop(AF_INET6, &addr_in6->sin6_addr, ip_str, sizeof(ip_str));
        //     ESP_LOGI(TAG, "IPv6 Address: %s, Port: %d", ip_str, ntohs(addr_in6->sin6_port));
        // }

        // Create a socket
        sock = socket(dns_res->ai_family, dns_res->ai_socktype, dns_res->ai_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Failed to create socket (%d): %s", errno, strerror(errno));
            continue;
        }

        // Set socket send timeout
        ret = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &sock_timeout, sizeof(sock_timeout));
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to set socket send timeout (%d): %s", errno, strerror(errno));
            close(sock);
            continue;
        }

        // Set socket receive timeout
        ret = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &sock_timeout, sizeof(sock_timeout));
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to set socket receive timeout (%d): %s", errno, strerror(errno));
            close(sock);
            continue;
        }

        // Connect to server
        ret = connect(sock, dns_res->ai_addr, dns_res->ai_addrlen);
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to connect to server (%d): %s", errno, strerror(errno));
            close(sock);
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

        // // Add port to the address
        // if (ip_addr.ai_family == AF_INET) {
        //     struct sockaddr_in *addr_in = (struct sockaddr_in *)ip_addr.ai_addr;
        //     addr_in->sin_port = htons(atoi(WEB_PORT));
        //     ESP_LOGI(TAG, "IPv4 Address: %s, Port: %d",
        //             inet_ntoa(addr_in->sin_addr), ntohs(addr_in->sin_port));
        // } else if (ip_addr.ai_family == AF_INET6) {
        //     struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)ip_addr.ai_addr;
        //     addr_in6->sin6_port = htons(atoi(WEB_PORT));
        //     char ip_str[INET6_ADDRSTRLEN];
        //     inet_ntop(AF_INET6, &addr_in6->sin6_addr, ip_str, sizeof(ip_str));
        //     ESP_LOGI(TAG, "IPv6 Address: %s, Port: %d", ip_str, ntohs(addr_in6->sin6_port));
        // }

        // // Create a socket
        // sock = socket(ip_addr.ai_family, ip_addr.ai_socktype, ip_addr.ai_protocol);
        // if (sock < 0) {
        //     ESP_LOGE(TAG, "Failed to create socket (%d): %s", errno, strerror(errno));
        //     continue;
        // }

        // // Set socket receive timeout
        // sock_ret = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &sock_timeout, sizeof(sock_timeout));
        // if (sock_ret < 0) {
        //     ESP_LOGE(TAG, "Failed to set socket receive timeout (%d): %s", errno, strerror(errno));
        //     close(sock);
        //     continue;
        // }

        // // Set socket send timeout
        // sock_ret = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &sock_timeout, sizeof(sock_timeout));
        // if (sock_ret < 0) {
        //     ESP_LOGE(TAG, "Failed to set socket send timeout (%d): %s", errno, strerror(errno));
        //     close(sock);
        //     continue;
        // }

        // // Connect to server
        // sock_ret = connect(sock, ip_addr.ai_addr, ip_addr.ai_addrlen);
        // if (sock_ret < 0) {
        //     ESP_LOGE(TAG, "Failed to connect to server (%d): %s", errno, strerror(errno));
        //     close(sock);
        //     continue;
        // }

        // // Send HTTP GET request
        // ESP_LOGI(TAG, "Sending HTTP GET request...");
        // sock_ret = send(sock, REQUEST, strlen(REQUEST), 0);
        // if (sock_ret < 0) {
        //     ESP_LOGE(TAG, "Failed to send HTTP GET request (%d): %s", errno, strerror(errno));
        //     close(sock);
        //     continue;
        // }

        // // Print the HTTP response
        // ESP_LOGI(TAG, "HTTP response:");
        // recv_total = 0;
        // while (1) {

        //     // Receive data from the socket
        //     recv_len = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);

        //     // Check for errors
        //     if (recv_len < 0) {
        //         ESP_LOGE(TAG, "Failed to receive data (%d): %s", errno, strerror(errno));
        //         break;
        //     }

        //     // Check for end of data
        //     if (recv_len == 0) {
        //         break;
        //     }

        //     // Null-terminate the received data and print it
        //     recv_buf[recv_len] = '\0';
        //     printf("%s", recv_buf);
        //     recv_total += (uint32_t)sock_ret;
        // }

        // // Close the socket
        // close(sock);

        // Wait before trying again
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}