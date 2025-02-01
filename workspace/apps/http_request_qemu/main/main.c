#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/netdb.h"

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

// Event handler: Ethernet events
static void on_eth_event(void *arg, 
                         esp_event_base_t event_base, 
                         int32_t event_id, 
                         void *event_data)
{
    const char *TAG = "eth";
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;
    uint8_t mac_addr[6] = {0};

    // Determine event type
    switch (event_id) {

        // Get MAC address when connected and print it
        case ETHERNET_EVENT_CONNECTED:
            esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
            ESP_LOGI(TAG, "Ethernet link up");
            ESP_LOGI(TAG, 
                     "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                     mac_addr[0], 
                     mac_addr[1], 
                     mac_addr[2], 
                     mac_addr[3], 
                     mac_addr[4], 
                     mac_addr[5]);
            break;
        
        // Print message when disconnected
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Ethernet disconnected");
            break;

        // Print message when started
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet started");
            break;

        // Print message when stopped
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet stopped");
            break;

        // Default case: do nothing
        default:
            break;
    }
}

// Event handler: received IP address on Ethernet interface from DHCP
static void on_got_ip_event(void *arg, 
                            esp_event_base_t event_base, 
                            int32_t event_id, 
                            void *event_data)
{
    const char *TAG = "eth";
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    // Print IP address
    ESP_LOGI(TAG, "Ethernet IP address obtained");
    ESP_LOGI(TAG, "  IP address:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "  Netmask:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "  Gateway:" IPSTR, IP2STR(&ip_info->gw));
}

// Initialize QEMU virtual Ethernet
esp_err_t init_qemu_eth(void)
{
    const char *TAG = "eth";
    esp_err_t esp_ret;
    esp_netif_t *eth_netif;
    esp_eth_mac_t *eth_mac = NULL;
    esp_eth_phy_t *eth_phy = NULL;
    esp_eth_handle_t eth_handle = NULL;
    esp_eth_netif_glue_handle_t eth_glue = NULL;

    // Initialize network interface
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&netif_config);
    if (!eth_netif) {
        ESP_LOGE(TAG, "Failed to create Ethernet interface");
        return ESP_FAIL;
    }

    // Configure physical layer (PHY) and create PHY instance
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.autonego_timeout_ms = 100;
    eth_phy = esp_eth_phy_new_dp83848(&phy_config);
    if (!eth_phy) {
        ESP_LOGE(TAG, "Failed to create PHY instance");
        return ESP_FAIL;
    }

    // Configure media access control (MAC) layer and create MAC instance
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_mac = esp_eth_mac_new_openeth(&mac_config);
    if (!eth_mac) {
        ESP_LOGE(TAG, "Failed to create MAC instance");
        return ESP_FAIL;
    }

    // Initialize Ethernet driver, connect MAC and PHY
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(eth_mac, eth_phy);
    esp_ret = esp_eth_driver_install(&eth_config, &eth_handle);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Ethernet driver");
        return esp_ret;
    }

    // TEST
    uint8_t eth_mac_addr[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(eth_mac_addr, ESP_MAC_ETH));
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, eth_mac_addr));

    // Create glue layer to attach Ethernet driver to network interface
    eth_glue = esp_eth_new_netif_glue(eth_handle);
    if (!eth_glue) {
        ESP_LOGE(TAG, "Failed to create glue layer");
        return ESP_FAIL;
    }

    // Attach Ethernet driver to network interface
    esp_ret = esp_netif_attach(eth_netif, eth_glue);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach Ethernet driver to network interface");
        return esp_ret;
    }

    // Register Ethernet event handler
    esp_ret = esp_event_handler_register(ETH_EVENT, 
                                         ESP_EVENT_ANY_ID, 
                                         &on_eth_event, 
                                         NULL);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register Ethernet event handler");
        return esp_ret;
    }

    // Register IP event handler
    esp_ret = esp_event_handler_register(IP_EVENT, 
                                         IP_EVENT_ETH_GOT_IP, 
                                         &on_got_ip_event, 
                                         NULL);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler");
        return esp_ret;
    }

    // Start Ethernet driver
    esp_ret = esp_eth_start(eth_handle);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Ethernet driver");
        return esp_ret;
    }

    return ESP_OK;
}

static void eth_stop(void)
{
//     esp_netif_t *eth_netif = get_example_netif_from_desc(EXAMPLE_NETIF_DESC_ETH);
//     ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_on_got_ip));
// #if CONFIG_EXAMPLE_CONNECT_IPV6
//     ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &eth_on_got_ipv6));
//     ESP_ERROR_CHECK(esp_event_handler_unregister(ETH_EVENT, ETHERNET_EVENT_CONNECTED, &on_eth_event));
// #endif
//     ESP_ERROR_CHECK(esp_eth_stop(s_eth_handle));
//     ESP_ERROR_CHECK(esp_eth_del_netif_glue(s_eth_glue));
//     ESP_ERROR_CHECK(esp_eth_driver_uninstall(s_eth_handle));
//     s_eth_handle = NULL;
//     ESP_ERROR_CHECK(s_phy->del(s_phy));
//     ESP_ERROR_CHECK(s_mac->del(s_mac));

//     esp_netif_destroy(eth_netif);
}

/*******************************************************************************
 * Main
 */

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
    esp_ret = init_qemu_eth();
    ESP_ERROR_CHECK(esp_ret);

    // Do forever: perform HTTP GET request
    while (1) {

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