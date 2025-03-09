/**
 * SPDX-FileCopyrightText: 2025 Shawn Hymel
 * SPDX-License-Identifier: Apache-2.0
 */

 #include <string.h>
 #include "esp_log.h"
 #include "esp_netif.h"
 #include "mqtt_client.h"
 #include "nvs_flash.h"

 #include "network_wrapper.h"

 // Tag for debug messages
static const char *TAG = "mqtts_demo";

// Network settings
#define CONNECTION_TIMEOUT_SEC  10  // Delay to wait for connection (sec)

// MQTT settings
#if CONFIG_WIFI_STA_CONNECT
# define MQTT_BROKER_HOSTNAME    "10.0.0.100"   // Host address on WiFi network
#elif CONFIG_ETHERNET_QEMU_CONNECT
#define MQTT_BROKER_HOSTNAME    "10.0.2.2"      // QEMU host IP address
#endif
#define MQTT_BROKER_PORT        8883
#define MQTT_COMMON_NAME        "localhost"
#define MQTT_USERNAME           "iot"
#define MQTT_PASSWORD           "mosquitto"
#define MQTT_QOS                2               // Quality of Service (0, 1, 2)
#define MQTT_TEST_TOPIC         "my_topic/sensor_data"
#define MQTT_TEST_MSG           "{\"temperature\": 25.0, \"humidity\": 50.0}"

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

        // Error in MQTT connection
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error:");
            ESP_LOGE(TAG, 
                     "  Error type: %d", 
                     event->error_handle->error_type);
            ESP_LOGE(TAG, 
                     "  Error return code: %d", 
                     event->error_handle->connect_return_code);
            ESP_LOGE(TAG, 
                     "  Socket errno: %d", 
                     event->error_handle->esp_transport_sock_errno);
            break;

        // Connected to MQTT broker
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to MQTT broker");
            xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
            break;

        // Disconnected from MQTT broker
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Disconnected from MQTT broker");
            xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
            break;

        // Subscribed to topic
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Subscribed to topic");
            break;

        // Unsubscribed from topic
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "Unsubscribed from topic");
            break;

        // Published message to broker
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "Published message to broker");
            break;

        // Received message from broker
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Received message from broker");
            ESP_LOGI(TAG, "  Topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "  Data: %.*s", event->data_len, event->data);
            break;

        // Before connecting to MQTT broker
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "Connecting to MQTT broker...");
            break;

        // Unhandled event
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

    //%%%TEST
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);

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

    // Configure MQTT client
    // esp_mqtt_client_config_t mqtt_cfg = {
    //     .broker.address.hostname = MQTT_BROKER_HOSTNAME,
    //     .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
    //     .broker.address.port = MQTT_BROKER_PORT,
    //     .broker.verification.use_global_ca_store = false,
    //     .broker.verification.certificate = (const char *)mqtt_ca_cert_start,
    //     .broker.verification.certificate_len = mqtt_ca_cert_end - mqtt_ca_cert_start,
    //     .broker.verification.common_name = MQTT_COMMON_NAME,
    //     .credentials.username = "iot",
    //     .credentials.authentication.password = "mosquitto",
    // };
    // %%%TEST%%%
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = "10.0.2.2",
        .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .broker.address.port = 8883,
        .broker.verification.use_global_ca_store = false,
        .broker.verification.certificate = (const char *)mqtt_ca_cert_start,
        .broker.verification.certificate_len = mqtt_ca_cert_end - mqtt_ca_cert_start,
        .broker.verification.skip_cert_common_name_check = false,
        .broker.verification.common_name = "localhost",
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
    xEventGroupWaitBits(s_mqtt_event_group, 
                        MQTT_CONNECTED_BIT, 
                        pdTRUE, 
                        pdTRUE, 
                        portMAX_DELAY);

    // Subscribe to a topic
    msg_id = esp_mqtt_client_subscribe(mqtt_client, 
                                       MQTT_TEST_TOPIC, 
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
                                         MQTT_TEST_TOPIC, 
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