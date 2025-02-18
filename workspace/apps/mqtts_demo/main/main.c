/**
 * SPDX-FileCopyrightText: 2025 Shawn Hymel
 * SPDX-License-Identifier: Apache-2.0
 */

 #include <string.h>
 #include "esp_log.h"
 #include "esp_netif.h"
 #include "mqtt_client.h"
 #include "nvs_flash.h"
 #include "mdns.h"

 #include "network_wrapper.h"

 // Tag for debug messages
static const char *TAG = "mqtt_demo";

// Network settings
#define CONNECTION_TIMEOUT_SEC  10  // Delay to wait for connection (sec)

// MQTT settings
#define MQTT_BROKER_URL         "mqtts://localhost"
#define MQTT_BROKER_PORT        8883
#define MQTT_USERNAME           "iot"
#define MQTT_PASSWORD           "mosquitto"
#define MQTT_QOS                2               // Quality of Service (0, 1, 2)
#define MQTT_PUBLISH_TOPIC      "/test/esp-pub"
#define MQTT_SUBSCRIBE_TOPIC    "/test/esp-sub"
#define MQTT_TEST_MSG           "Hello, MQTT!"

// Event group bits
#define MQTT_CONNECTED_BIT      BIT0

// Load CA certificate from binary data
extern const uint8_t mqtt_ca_cert_start[]   asm("_binary_ca_crt_start");
extern const uint8_t mqtt_ca_cert_end[]     asm("_binary_ca_crt_end");

// Static global variables
static EventGroupHandle_t s_mqtt_event_group = NULL;

// MQTT event handler
static void mqtt_event_handler(void *handler_args, 
                               esp_event_base_t base, 
                               int32_t event_id, 
                               void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    // Determine event type
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to MQTT broker");
            xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Disconnected from MQTT broker");
            xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Subscribed to topic");
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "Unsubscribed from topic");
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "Published message to broker");
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Received message from broker");
            ESP_LOGI(TAG, "  Topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "  Data: %.*s", event->data_len, event->data);
            break;
        default:
            ESP_LOGI(TAG, "Unhandled MQTT event: %li", event_id);
            break;
    }
}

// Main app entrypoint
void app_main(void)
{
    esp_err_t esp_ret;
    int msg_id;
    EventGroupHandle_t network_event_group;

    // %%%TEST
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    // Welcome message (after delay to allow serial connection)
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Starting MQTT demo");

    // Initialize event groups
    network_event_group = xEventGroupCreate();
    s_mqtt_event_group = xEventGroupCreate();

    // Initialize NVS: ESP32 WiFi driver uses NVS to store WiFi settings
    // Erase NVS partition if it's out of free space or new version
    esp_ret = nvs_flash_init();
    if (esp_ret == ESP_ERR_NVS_NO_FREE_PAGES || 
        esp_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS partition");
            abort();
        }
        esp_ret = nvs_flash_init();
    }
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS (%d)", esp_ret);
        abort();
    }

    // Initialize TCP/IP network interface (only call once in application)
    // Must be called prior to initializing the network driver!
    esp_ret = esp_netif_init();
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize network interface (%d)", esp_ret);
        abort();
    }

    // Create default event loop that runs in the background
    // Must be running prior to initializing the network driver!
    esp_ret = esp_event_loop_create_default();
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create default event loop (%d)", esp_ret);
        abort();
    }

    // Initialize network connection
    esp_ret = network_init(network_event_group);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize network (%d)", esp_ret);
        abort();
    }

    // Make sure network is connected and device has an IP address
    while (!wait_for_network(network_event_group, CONNECTION_TIMEOUT_SEC)) {
        ESP_LOGE(TAG, "Failed to connect to WiFi. Reconnecting...");
        esp_ret = network_reconnect();
        if (esp_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reconnect WiFi (%d)", esp_ret);
            abort();
        }
    }

    // Initialize mDNS
    esp_ret = mdns_init();
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mDNS (%d)", esp_ret);
        abort();
    }

    // Get IP address of mosquitto server
    esp_ip4_addr_t addr;

    esp_err_t err = mdns_query_a("mosquitto", 2000,  &addr);
    if(err){
        if(err == ESP_ERR_NOT_FOUND){
            printf("Host was not found!");
            return;
        }
        printf("Query Failed");
        return;
    }

    // Print IP address
    char ip_str[16];
    esp_ip4addr_ntoa(&addr, ip_str, sizeof(ip_str));
    printf("IP address: %s\n", ip_str);

    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {
        // .broker.address.uri = MQTT_BROKER_URL,
        .broker.address.hostname = "mosquitto.local",
        .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .broker.address.port = 8883,
        .broker.verification.use_global_ca_store = false,
        .broker.verification.certificate = (const char *)mqtt_ca_cert_start,
        .broker.verification.certificate_len = mqtt_ca_cert_end - mqtt_ca_cert_start,
        .credentials.username = "iot",
        .credentials.authentication.password = "mosquitto",
    };

    // Initialize MQTT client
    esp_mqtt_client_handle_t mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

    // Register event handler
    esp_ret = esp_mqtt_client_register_event(mqtt_client,
                                             ESP_EVENT_ANY_ID, 
                                             mqtt_event_handler, 
                                             NULL);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler (%d)", esp_ret);
        ESP_ERROR_CHECK(esp_mqtt_client_destroy(mqtt_client));
        abort();
    }

    // Start MQTT client
    esp_ret = esp_mqtt_client_start(mqtt_client);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client (%d)", esp_ret);
        ESP_ERROR_CHECK(esp_mqtt_client_destroy(mqtt_client));
        abort();
    }

    // Wait for MQTT client to connect
    ESP_LOGI(TAG, "Waiting to connecting to MQTT broker...");
    xEventGroupWaitBits(s_mqtt_event_group, 
                        MQTT_CONNECTED_BIT, 
                        pdTRUE, 
                        pdTRUE, 
                        portMAX_DELAY);
    ESP_LOGI(TAG, "Connected to MQTT broker");

    // Subscribe to a topic
    msg_id = esp_mqtt_client_subscribe(mqtt_client, 
                                       MQTT_SUBSCRIBE_TOPIC, 
                                       MQTT_QOS);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Error (%d): Failed to subscribe to topic", msg_id);
        ESP_ERROR_CHECK(esp_mqtt_client_destroy(mqtt_client));
        abort();
    }

    // Main loop
    while (1) {

        // Publish message to MQTT broker
        msg_id = esp_mqtt_client_publish(mqtt_client, 
                                         MQTT_PUBLISH_TOPIC, 
                                         MQTT_TEST_MSG, 
                                         0,         // Length (0 = auto detect)
                                         MQTT_QOS,  // QoS
                                         0);        // Retain
        if (msg_id < 0) {
            ESP_LOGE(TAG, "Error (%d): Failed to publish message", msg_id);
        }

        // Wait before publishing another message
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}