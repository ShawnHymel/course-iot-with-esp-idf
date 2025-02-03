#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_private/wifi.h"
#include "esp_wifi.h"
#include "esp_wifi_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/netdb.h"

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

// Static globals
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_wifi_netif = NULL;

// TEST: start WiFi
static void wifi_start(void *esp_netif, esp_event_base_t base, int32_t event_id, void *data)
{
    uint8_t mac[6];
    esp_err_t ret;

    ESP_LOGD(TAG, "%s esp-netif:%p event-id%" PRId32 "", __func__, esp_netif, event_id);

    wifi_netif_driver_t driver = esp_netif_get_io_driver(esp_netif);

    if ((ret = esp_wifi_get_if_mac(driver, mac)) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_get_mac failed with %d", ret);
        return;
    }
    ESP_LOGD(TAG, "WIFI mac address: %x %x %x %x %x %x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (esp_wifi_is_if_ready_when_started(driver)) {
        if ((ret = esp_wifi_register_if_rxcb(driver,  esp_netif_receive, esp_netif)) != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_register_if_rxcb for if=%p failed with %d", driver, ret);
            return;
        }
    }

    if ((ret = esp_wifi_internal_reg_netstack_buf_cb(esp_netif_netstack_buf_ref, esp_netif_netstack_buf_free)) != ESP_OK) {
        ESP_LOGE(TAG, "netstack cb reg failed with %d", ret);
        return;
    }
    esp_netif_set_mac(esp_netif, mac);
    esp_netif_action_start(esp_netif, base, event_id, data);
}

// Event handler: WiFi events
static void on_wifi_event(void *arg, 
                         esp_event_base_t event_base, 
                         int32_t event_id, 
                         void *event_data)
{
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

// Event handler: IP events
static void on_ip_event(void *arg, 
                        esp_event_base_t event_base, 
                        int32_t event_id, 
                        void *event_data)
{
    esp_err_t esp_ret;

    // Determine event type
    switch(event_id) {

        // Got IP address
        case IP_EVENT_STA_GOT_IP:
            if (s_wifi_netif != NULL) {
                esp_ret = esp_wifi_internal_set_sta_ip();
                if (esp_ret != ESP_OK) {
                    ESP_LOGI(TAG, "Failed to set IP address");
                }
                esp_netif_action_got_ip(s_wifi_netif, event_base, event_id, event_data);

                // Print IP address
                ip_event_got_ip_t *event_ip = (ip_event_got_ip_t *)event_data;
                ESP_LOGI(TAG, "Got IPv4 address:");
                ESP_LOGI(TAG, "  IP address: " IPSTR, IP2STR(&event_ip->ip_info.ip));
                ESP_LOGI(TAG, "  Netmask: " IPSTR, IP2STR(&event_ip->ip_info.netmask));
                ESP_LOGI(TAG, "  Gateway: " IPSTR, IP2STR(&event_ip->ip_info.gw));

                // Notify that WiFi is connected
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            }
            break;

        // Got IPv6 address
        case IP_EVENT_GOT_IP6:
            ip_event_got_ip6_t *event_ip6 = (ip_event_got_ip6_t *)event_data;
            ESP_LOGI(TAG, "Got IPv6 address: " IPV6STR, IPV62STR(event_ip6->ip6_info.ip));
            break;
    }
}

// // Called on WiFi events
// static void wifi_event_handler(void* arg, 
//                                esp_event_base_t event_base,
//                                int32_t event_id, 
//                                void* event_data)
// {
//     // Try to connect to WiFi AP
//     if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
//         ESP_LOGI(TAG, "Connecting to %s...", ESP_WIFI_SSID);
//         esp_wifi_connect();

//         // %%%TEST: IPv6
//         // esp_netif_create_ip6_linklocal(s_wifi_netif);

//     // If disconnected, try to reconnect in a few seconds
//     } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
//         xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
//         ESP_LOGI(TAG, 
//                  "WiFi disconnected. Waiting %i seconds to reconnect.", 
//                   RECONNECT_DELAY_SEC);
//         vTaskDelay((RECONNECT_DELAY_SEC * 1000) / portTICK_PERIOD_MS);
//         ESP_LOGI(TAG, "Reconnecting to %s...", ESP_WIFI_SSID);
//         esp_wifi_connect();

//     // If connected, print IP address and set event group bit
//     } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
//         ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
//         ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
//         xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

//     // If got IPv6 address, print it
//     } else if (event_base == IP_EVENT && event_id == IP_EVENT_GOT_IP6) {
//         ip_event_got_ip6_t* event_ip6 = (ip_event_got_ip6_t*)event_data;
//         ESP_LOGI(TAG, "Got IPv6 address: " IPV6STR, IPV62STR(event_ip6->ip6_info.ip));
//     }
// }

// Initialize WiFi in station (STA) mode
esp_err_t wifi_init_sta(void)
{
    esp_err_t esp_ret;

    // Create event group
    s_wifi_event_group = xEventGroupCreate();

    // Initialize TCP/IP stack
    esp_ret = esp_netif_init();
    ESP_ERROR_CHECK(esp_ret);

    // Create default event loop
    esp_ret = esp_event_loop_create_default();
    ESP_ERROR_CHECK(esp_ret);

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_ret = esp_wifi_init(&cfg);
    ESP_ERROR_CHECK(esp_ret);

    // esp_netif_create_default_wifi_sta();
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_WIFI_STA();
    s_wifi_netif = esp_netif_new(&netif_cfg);
    assert(s_wifi_netif);

    // ESP_ERROR_CHECK(esp_netif_attach_wifi_station(s_wifi_netif));
    wifi_netif_driver_t driver = esp_wifi_create_if_driver(WIFI_IF_STA);
    if (driver == NULL) {
        ESP_LOGE(TAG, "Failed to create wifi interface handle");
        return ESP_FAIL;
    }

    // Attach WiFi driver to network interface
    esp_ret = esp_netif_attach(s_wifi_netif, driver);
    ESP_ERROR_CHECK(esp_ret);




    // ESP_ERROR_CHECK(esp_wifi_set_default_wifi_sta_handlers());
    esp_ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, &on_wifi_event, NULL);
    ESP_ERROR_CHECK(esp_ret);
    esp_ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_STOP, &on_wifi_event, NULL);
    ESP_ERROR_CHECK(esp_ret);
    esp_ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_event, NULL);
    ESP_ERROR_CHECK(esp_ret);
    esp_ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_event, NULL);
    ESP_ERROR_CHECK(esp_ret);
    esp_ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL);
    ESP_ERROR_CHECK(esp_ret);
    esp_ret = esp_register_shutdown_handler((shutdown_handler_t)esp_wifi_stop);
    ESP_ERROR_CHECK(esp_ret);

    //Initialize network interface for WiFi in station mode
    // esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    // s_wifi_netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    // if (s_wifi_netif == NULL) {
    //     ESP_LOGE(TAG, "Failed to create WiFi network interface");
    //     return;
    // }

    // // Initialize WiFi station mode
    // esp_netif_config_t wifi_cfg = ESP_NETIF_DEFAULT_WIFI_STA();
    // s_wifi_netif = esp_netif_new(&wifi_cfg);
    // assert(s_wifi_netif);
    // ESP_ERROR_CHECK(esp_netif_attach_wifi_station(s_wifi_netif));
    // // ESP_ERROR_CHECK(esp_wifi_set_default_wifi_sta_handlers());

    // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // ret = esp_wifi_init(&cfg);
    // ESP_ERROR_CHECK(ret);

    // // Register WiFi event handler
    // ret = esp_event_handler_instance_register(WIFI_EVENT,
    //                                           ESP_EVENT_ANY_ID,
    //                                           &wifi_event_handler,
    //                                           NULL,
    //                                           &event_any_id);
    // ESP_ERROR_CHECK(ret);

    // // Register IP address event handler
    // ret = esp_event_handler_instance_register(IP_EVENT,
    //                                           IP_EVENT_STA_GOT_IP,
    //                                           &wifi_event_handler,
    //                                           NULL,
    //                                           &event_got_ip);
    // ESP_ERROR_CHECK(ret);
        
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
    esp_ret = esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_ERROR_CHECK(esp_ret);
    esp_ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    ESP_ERROR_CHECK(esp_ret);
    esp_ret = esp_wifi_start();
    ESP_ERROR_CHECK(esp_ret);

    // Connect to WiFi
    ESP_LOGI(TAG, "Connecting to %s...", ESP_WIFI_SSID);
    esp_ret = esp_wifi_connect();
    ESP_ERROR_CHECK(esp_ret);


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
    esp_ret = wifi_init_sta();
    ESP_ERROR_CHECK(esp_ret);

    // Do forever: perform HTTP GET request
    while (1) {

        // Perform DNS lookup
        ret = getaddrinfo(WEB_HOST, WEB_PORT, &hints, &dns_res);
        if (ret != 0 || dns_res== NULL) {
            ESP_LOGE(TAG, "DNS lookup failed (%d)", ret);
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

        // Wait before trying again
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}