/*
 * SPDX-FileCopyrightText: 2025 Shawn Hymel
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * Sets up the WiFi interface and registers event handlers.
 * 
 * For more information on the ESP32 WiFi API, see:
 * https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32/api-guides/wifi.html
 *
 * Tags, such as (s2.3), correspond to sections in the ESP32 WiFi API guide.
 */

#include "esp_event.h"
#include "esp_log.h"
#include "esp_private/wifi.h"
#include "esp_wifi.h"
#include "esp_wifi_netif.h"

#include "wifi_sta.h"

// Tag for debug messages
static const char *TAG = "wifi_sta";

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
    esp_err_t esp_ret;

    // Determine event type
    switch(event_id) {

        // WiFi started
        case WIFI_EVENT_STA_START:
            if (s_wifi_netif != NULL) {
                wifi_start(s_wifi_netif, event_base, event_id, event_data);
            }
            break;

        // WiFi stopped
        case WIFI_EVENT_STA_STOP:
            if (s_wifi_netif != NULL) {
                esp_netif_action_stop(s_wifi_netif, 
                                      event_base, 
                                      event_id, 
                                      event_data);
            }
            break;

        // Connected to WiFi
        case WIFI_EVENT_STA_CONNECTED:

            // Make sure we have a valid interface handle
            if (s_wifi_netif == NULL) {
                ESP_LOGE(TAG, "WiFi not started: interface handle is NULL");
                return;
            }

            // Print AP information
            wifi_event_sta_connected_t *event_sta_connected = 
                (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "Connected to AP");
            ESP_LOGI(TAG, "  SSID: %s", (char *)event_sta_connected->ssid);
            ESP_LOGI(TAG, "  Channel: %d", event_sta_connected->channel);
            ESP_LOGI(TAG, "  Auth mode: %d", event_sta_connected->authmode);
            ESP_LOGI(TAG, "  AID: %d", event_sta_connected->aid);

            // (s4.2) Register interface receive callback
            wifi_netif_driver_t driver = esp_netif_get_io_driver(s_wifi_netif);
            if (!esp_wifi_is_if_ready_when_started(driver)) {
                esp_ret = esp_wifi_register_if_rxcb(driver,
                                                    esp_netif_receive,
                                                    s_wifi_netif);
                if (esp_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to register WiFi RX callback");
                    return;
                }
            }

            // (s4.2) Set up the WiFi interface and start DHCP process
            esp_netif_action_connected(s_wifi_netif, 
                                       event_base, 
                                       event_id, 
                                       event_data);

            // Set WiFi connected bit
            xEventGroupSetBits(s_wifi_event_group, WIFI_STA_CONNECTED_BIT);

#if CONFIG_WIFI_STA_CONNECT_IPV6 || WIFI_STA_CONNECT_UNSPECIFIED
            // Request IPv6 link-local address for the interface
            esp_ret = esp_netif_create_ip6_linklocal(s_wifi_netif);
            if (esp_ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to create IPv6 link-local address");
            }
#endif
            break;

        // Disconnected from WiFi
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_wifi_netif != NULL) {
                esp_netif_action_disconnected(s_wifi_netif,
                                              event_base,
                                              event_id,
                                              event_data);
            }
            xEventGroupClearBits(s_wifi_event_group, WIFI_STA_CONNECTED_BIT);
            ESP_LOGI(TAG, "WiFi disconnected");
#if CONFIG_WIFI_STA_AUTO_RECONNECT
            ESP_LOGI(TAG, "Attempting to reconnect...");
            wifi_sta_reconnect();
#endif
            break;

        // Default case: do nothing
        default:
            ESP_LOGI(TAG, "Unhandled WiFi event: %li", event_id);
            break;
    }
}

// Event handler: received IP address on WiFi interface from DHCP
static void on_ip_event(void *arg, 
                        esp_event_base_t event_base, 
                        int32_t event_id, 
                        void *event_data)
{

    esp_err_t esp_ret;

    // Determine event type
    switch(event_id) {

#if CONFIG_WIFI_STA_CONNECT_IPV4 || WIFI_STA_CONNECT_UNSPECIFIED
        // (s5.2) Got IPv4 address
        case IP_EVENT_STA_GOT_IP:

            // Make sure we have a valid interface handle
            if (s_wifi_netif == NULL) {
                ESP_LOGE(TAG, "On obtain IPv4 addr: Interface handle is NULL");
                return;
            }

            // Notify the WiFi driver that we got an IP address
            esp_ret = esp_wifi_internal_set_sta_ip();
            if (esp_ret != ESP_OK) {
                ESP_LOGI(TAG, "Failed to notify WiFi driver of IP address");
            }

            // Set connected bit
            xEventGroupSetBits(s_wifi_event_group, WIFI_STA_IPV4_OBTAINED_BIT);

            // Print IP address
            ip_event_got_ip_t *event_ip = (ip_event_got_ip_t *)event_data;
            esp_netif_ip_info_t *ip_info = &event_ip->ip_info;
            ESP_LOGI(TAG, "WiFi IPv4 address obtained");
            ESP_LOGI(TAG, "  IP address: " IPSTR, IP2STR(&ip_info->ip));
            ESP_LOGI(TAG, "  Netmask: " IPSTR, IP2STR(&ip_info->netmask));
            ESP_LOGI(TAG, "  Gateway: " IPSTR, IP2STR(&ip_info->gw));

            break;
#endif

#if CONFIG_WIFI_STA_IPV6 || WIFI_STA_CONNECT_UNSPECIFIED
        // (s5.2) Got IPv6 address
        case IP_EVENT_GOT_IP6:

            // Make sure we have a valid interface handle
            if (s_wifi_netif == NULL) {
                ESP_LOGE(TAG, "On obtain IPv6 addr: Interface handle is NULL");
                return;
            }

            // Notify the WiFi driver that we got an IP address
            esp_ret = esp_wifi_internal_set_sta_ip();
            if (esp_ret != ESP_OK) {
                ESP_LOGI(TAG, "Failed to notify WiFi driver of IP address");
            }

            // Set connected bit
            xEventGroupSetBits(s_wifi_event_group, WIFI_STA_IPV6_OBTAINED_BIT);

            // Print IPv6 address
            ip_event_got_ip6_t *event_ipv6 = (ip_event_got_ip6_t *)event_data;
            esp_netif_ip6_info_t *ip6_info = &event_ipv6->ip6_info;
            ESP_LOGI(TAG, "Ethernet IPv6 address obtained");
            ESP_LOGI(TAG, "  IP address: " IPV6STR, IPV62STR(ip6_info->ip));

            break;
#endif

        // Lost IP address
        case IP_EVENT_STA_LOST_IP:
            xEventGroupClearBits(s_wifi_event_group, WIFI_STA_IPV4_OBTAINED_BIT);
            xEventGroupClearBits(s_wifi_event_group, WIFI_STA_IPV6_OBTAINED_BIT);
            ESP_LOGI(TAG, "WiFi lost IP address");
            break;

        // Default case: do nothing
        default:
            ESP_LOGI(TAG, "Unhandled IP event: %li", event_id);
            break;
    }
}

// Set up the WiFi interface and start DHCP process (called from on_wifi_event)
static void wifi_start(void *esp_netif, 
                       esp_event_base_t base,
                       int32_t event_id, 
                       void *data)
{
    uint8_t mac_addr[6] = {0};
    esp_err_t esp_ret;

    // (s1.3) Get esp-netif driver handle
    wifi_netif_driver_t driver = esp_netif_get_io_driver(esp_netif);
    if (driver == NULL) {
        ESP_LOGE(TAG, "Failed to get WiFi driver handle");
        return;
    }

    // (s1.3) Get MAC address of WiFi interface
    if ((esp_ret = esp_wifi_get_if_mac(driver, mac_addr)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC address");
        return;
    }
    ESP_LOGI(TAG, 
             "WiFi MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], 
             mac_addr[1], 
             mac_addr[2], 
             mac_addr[3], 
             mac_addr[4], 
             mac_addr[5]);

    // (s1.3) Register interface receive callback
    if (esp_wifi_is_if_ready_when_started(driver)) {
        esp_ret = esp_wifi_register_if_rxcb(driver,
                                            esp_netif_receive,
                                            esp_netif);
        if (esp_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register WiFi RX callback");
            return;
        }
    }

    // (s1.3) Register netstack buffer reference and free callback
    esp_ret = esp_wifi_internal_reg_netstack_buf_cb(esp_netif_netstack_buf_ref, 
                                                    esp_netif_netstack_buf_free);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "netstack cb reg failed with %d", esp_ret);
        return;
    }

    // (s1.3) Set MAC address of the WiFi interface
    esp_ret = esp_netif_set_mac(esp_netif, mac_addr);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MAC address");
        return;
    }

    // (s1.3) Start the WiFi interface
    esp_netif_action_start(esp_netif, base, event_id, data);

    // (s3.3) Connect to WiFi
    ESP_LOGI(TAG, "Connecting to %s...", CONFIG_WIFI_STA_SSID);
    esp_ret = esp_wifi_connect();
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
    }
}

/*******************************************************************************
 * Public function definitions
 */

// Initialize WiFi in station (STA) mode
esp_err_t wifi_sta_init(EventGroupHandle_t event_group)
{
    esp_err_t esp_ret;

    // Print message
    ESP_LOGI(TAG, "Starting WiFi in station mode...");

    // Save the event group handle
    if (event_group != NULL) {
        s_wifi_event_group = event_group;
    }
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Event group handle is NULL");
        return ESP_FAIL;
    }

    // (s1.3) Create default WiFi network interface
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_WIFI_STA();
    s_wifi_netif = esp_netif_new(&netif_cfg);
    if (s_wifi_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi network interface");
        return ESP_FAIL;
    }

    // (s1.3) Create WiFi driver
    wifi_netif_driver_t driver = esp_wifi_create_if_driver(WIFI_IF_STA);
    if (driver == NULL) {
        ESP_LOGE(TAG, "Failed to create wifi interface handle");
        return ESP_FAIL;
    }

    // (s1.3) Connect WiFi driver to network interface
    esp_ret = esp_netif_attach(s_wifi_netif, driver);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach WiFi driver to network interface");
        return ESP_FAIL;
    }

    // (s1.3) Register Wifi event: station start
    esp_ret = esp_event_handler_register(WIFI_EVENT, 
                                         WIFI_EVENT_STA_START,
                                         &on_wifi_event, 
                                         NULL);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi STA start event handler");
        return ESP_FAIL;
    }

    // (s1.3) Register Wifi event: station stop
    esp_ret = esp_event_handler_register(WIFI_EVENT, 
                                         WIFI_EVENT_STA_STOP,
                                         &on_wifi_event, 
                                         NULL);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi STA stop event handler");
        return ESP_FAIL;
    }

    // (s1.3) Register Wifi event: station connected
    esp_ret = esp_event_handler_register(WIFI_EVENT, 
                                         WIFI_EVENT_STA_CONNECTED,
                                         &on_wifi_event, 
                                         NULL);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi STA connected event handler");
        return ESP_FAIL;
    }

    // (s1.3) Register Wifi event: station disconnected
    esp_ret = esp_event_handler_register(WIFI_EVENT, 
                                         WIFI_EVENT_STA_DISCONNECTED,
                                         &on_wifi_event, 
                                         NULL);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi STA disconnected event handler");
        return ESP_FAIL;
    }

#if CONFIG_WIFI_STA_CONNECT_IPV4 || CONFIG_WIFI_STA_CONNECT_UNSPECIFIED
    // (s1.3) Register IP event: station got IPv4 address
    esp_ret = esp_event_handler_register(IP_EVENT, 
                                         IP_EVENT_STA_GOT_IP,
                                         &on_ip_event, 
                                         NULL);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IPv4 event handler");
        return ESP_FAIL;
    }
#endif

#if CONFIG_WIFI_STA_CONNECT_IPV6 || CONFIG_WIFI_STA_CONNECT_UNSPECIFIED
    // (s1.3) Register IP event: station got IPv6 address
    esp_ret = esp_event_handler_register(IP_EVENT, 
                                         IP_EVENT_GOT_IP6,
                                         &on_ip_event, 
                                         NULL);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IPv6 event handler");
        return ESP_FAIL;
    }
#endif

    // (s1.3) Register shutdown handler
    esp_ret = esp_register_shutdown_handler((shutdown_handler_t)esp_wifi_stop);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register shutdown handler");
        return ESP_FAIL;
    }

    // (s1.4) Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_ret = esp_wifi_init(&cfg);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi");
        return ESP_FAIL;
    }

    // (s2) Set WiFi mode to station (device)
    esp_ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode to station");
        return ESP_FAIL;
    }

    // Configure minimum WiFi authentication mode
#if CONFIG_WIFI_STA_AUTH_OPEN
    const wifi_auth_mode_t auth_mode = WIFI_AUTH_OPEN;
#elif CONFIG_WIFI_STA_AUTH_WEP
    const wifi_auth_mode_t auth_mode = WIFI_AUTH_WEP;
#elif CONFIG_WIFI_STA_AUTH_WPA_PSK
    const wifi_auth_mode_t auth_mode = WIFI_AUTH_WPA_PSK;
#elif CONFIG_WIFI_STA_AUTH_WPA2_PSK
    const wifi_auth_mode_t auth_mode = WIFI_AUTH_WPA2_PSK;
#elif CONFIG_WIFI_STA_AUTH_WPA_WPA2_PSK
    const wifi_auth_mode_t auth_mode = WIFI_AUTH_WPA_WPA2_PSK;
#elif CONFIG_WIFI_STA_AUTH_WPA3_PSK
    const wifi_auth_mode_t auth_mode = WIFI_AUTH_WPA3_PSK;
#elif CONFIG_WIFI_STA_AUTH_WPA2_WPA3_PSK
    const wifi_auth_mode_t auth_mode = WIFI_AUTH_WPA2_WPA3_PSK;
#elif CONFIG_WIFI_STA_AUTH_WAPI_PSK
    const wifi_auth_mode_t auth_mode = WIFI_AUTH_WAPI_PSK;
#else
    const wifi_auth_mode_t auth_mode = WIFI_AUTH_OPEN;
#endif

    // Configure WPA3 SAE mode
#if CONFIG_WIFI_STA_WPA3_SAE_PWE_HUNT_AND_PECK
    const wifi_sae_pwe_method_t sae_pwe_method = WPA3_SAE_PWE_HUNT_AND_PECK;
#elif CONFIG_WIFI_STA_WPA3_SAE_PWE_H2E
    const wifi_sae_pwe_method_t sae_pwe_method = WPA3_SAE_PWE_HASH_TO_ELEMENT;
#elif CONFIG_WIFI_STA_WPA3_SAE_PWE_BOTH
    const wifi_sae_pwe_method_t sae_pwe_method = WPA3_SAE_PWE_BOTH;
#else
    const wifi_sae_pwe_method_t sae_pwe_method = WPA3_SAE_PWE_UNSPECIFIED;
#endif

    // (s2) Configure WiFi connection
    // Note: You can save these settings in NVS for future use if you are e.g.
    // using a provisioning app to set up the device
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_STA_SSID,
            .password = CONFIG_WIFI_STA_PASSWORD,
            .threshold.authmode = auth_mode,
            .sae_pwe_h2e = sae_pwe_method,
            .sae_h2e_identifier = CONFIG_WIFI_STA_WPA3_PASSWORD_ID,
        },
    };
    esp_ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi configuration");
        return ESP_FAIL;
    }

    // (s3.1) Start the WiFi driver
    esp_ret = esp_wifi_start();
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi driver");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// Stop WiFi
esp_err_t wifi_sta_stop(void)
{
    esp_err_t esp_ret;

    // Print message
    ESP_LOGI(TAG, "Stopping WiFi...");

    // Unregister event: station start
    esp_ret = esp_event_handler_unregister(WIFI_EVENT, 
                                          WIFI_EVENT_STA_START, 
                                          &on_wifi_event);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister WiFi STA start event handler");
        return ESP_FAIL;
    }

    // Unregister event: station stop
    esp_ret = esp_event_handler_unregister(WIFI_EVENT, 
                                          WIFI_EVENT_STA_STOP, 
                                          &on_wifi_event);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister WiFi STA stop event handler");
        return ESP_FAIL;
    }

    // Unregister event: station connected
    esp_ret = esp_event_handler_unregister(WIFI_EVENT, 
                                          WIFI_EVENT_STA_CONNECTED, 
                                          &on_wifi_event);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister WiFi STA connected event handler");
        return ESP_FAIL;
    }

    // Unregister event: station disconnected
    esp_ret = esp_event_handler_unregister(WIFI_EVENT, 
                                          WIFI_EVENT_STA_DISCONNECTED, 
                                          &on_wifi_event);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister WiFi STA disconnected event " \
                      "handler");
        return ESP_FAIL;
    }

#if CONFIG_WIFI_STA_CONNECT_IPV4 || CONFIG_WIFI_STA_CONNECT_UNSPECIFIED
    // Unregister event: station got IPv4 address
    esp_ret = esp_event_handler_unregister(IP_EVENT, 
                                          IP_EVENT_STA_GOT_IP, 
                                          &on_ip_event);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister IPv4 event handler");
        return ESP_FAIL;
    }
#endif

#if CONFIG_WIFI_STA_CONNECT_IPV6 || CONFIG_WIFI_STA_CONNECT_UNSPECIFIED
    // Unregister event: station got IPv6 address
    esp_ret = esp_event_handler_unregister(IP_EVENT, 
                                          IP_EVENT_GOT_IP6, 
                                          &on_ip_event);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister IPv6 event handler");
        return ESP_FAIL;
    }
#endif

    // Unregister shutdown handler
    esp_ret = esp_unregister_shutdown_handler(
        (shutdown_handler_t)esp_wifi_stop);
    if (esp_ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "Shutdown handler already unregistered");
    } else if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister shutdown handler");
        return ESP_FAIL;
    }

    // (s8.1) Disconnect from WiFi
    esp_ret = esp_wifi_disconnect();
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect from WiFi (%d)", esp_ret);
        return ESP_FAIL;
    }

    // (s8.2) Stop the WiFi driver
    esp_ret = esp_wifi_stop();
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop WiFi driver");
        return ESP_FAIL;
    }

    // (s8.3) Unload the WiFi driver, free resources
    esp_ret = esp_wifi_deinit();
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize WiFi");
        return ESP_FAIL;
    }

    // Destroy network interface
    if (s_wifi_netif != NULL) {
        esp_netif_destroy(s_wifi_netif);
    }

    // Clear event group bits
    if (s_wifi_event_group != NULL) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_STA_CONNECTED_BIT);
        xEventGroupClearBits(s_wifi_event_group, WIFI_STA_IPV4_OBTAINED_BIT);
        xEventGroupClearBits(s_wifi_event_group, WIFI_STA_IPV6_OBTAINED_BIT);
    }

    // Set handles to NULL
    s_wifi_netif = NULL;

    // Print message
    ESP_LOGI(TAG, "WiFi stopped");

    return ESP_OK;
}

// Attempt reconnection to WiFi
esp_err_t wifi_sta_reconnect(void)
{
    esp_err_t esp_ret;

    // Stop WiFi
    esp_ret = wifi_sta_stop();
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop WiFi during reconnect");
        return esp_ret;
    }

    // Start WiFi
    esp_ret = wifi_sta_init(NULL);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi during reconnect");
        return esp_ret;
    }

    return ESP_OK;
}