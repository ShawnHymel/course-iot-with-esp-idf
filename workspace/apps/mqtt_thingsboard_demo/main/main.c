#include <string.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#include "network_wrapper.h"

// Settings
static const uint32_t sleep_time_ms = 5000;
#define CONNECTION_TIMEOUT_SEC  10

// MQTT settings
#define MQTT_BROKER_HOSTNAME    "demo.thingsboard.io"
#define MQTT_BROKER_PORT        1883
#define MQTT_USERNAME           "fs1t8ma6vpu3ziwenm6l"
#define MQTT_PASSWORD           ""
#define MQTT_PUB_QOS            1   // Quality of Service (0, 1, 2)
#define MQTT_PUB_TOPIC          "v1/devices/me/telemetry"
#define MQTT_MSG                "{\"temp\": 25}"

// Event group bits
#define MQTT_CONNECTED_BIT      BIT0

// Tag for debug messages
static const char *TAG = "mqtt_thingsboard_demo";

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
           ESP_LOGE(TAG, "  Error type: %d", event->error_handle->error_type);
           ESP_LOGE(TAG, "  Error return code: %d", event->error_handle->connect_return_code);
           ESP_LOGE(TAG, "  Socket errno: %d", event->error_handle->esp_transport_sock_errno);
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

   // Initialize event groups
   network_event_group = xEventGroupCreate();
   s_mqtt_event_group = xEventGroupCreate();

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
   esp_mqtt_client_config_t mqtt_cfg = {
       .broker.address.hostname = MQTT_BROKER_HOSTNAME,
       .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
       .broker.address.port = MQTT_BROKER_PORT,
       .credentials.username = MQTT_USERNAME,
       .credentials.authentication.password = MQTT_PASSWORD,
   };

   // Initialize MQTT client
   esp_mqtt_client_handle_t mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

   // Register event handler
   esp_ret = esp_mqtt_client_register_event(mqtt_client,
                                            ESP_EVENT_ANY_ID, 
                                            mqtt_event_handler, 
                                            NULL);
   if (esp_ret != ESP_OK) {
       ESP_LOGE(TAG, "Error (%d): Failed to register MQTT event handler", esp_ret);
       ESP_ERROR_CHECK(esp_mqtt_client_destroy(mqtt_client));
       abort();
   }

   // Start MQTT client
   ESP_LOGI(TAG, "Connecting to MQTT server...");
   esp_ret = esp_mqtt_client_start(mqtt_client);
   if (esp_ret != ESP_OK) {
       ESP_LOGE(TAG, "Error (%d): Failed to start MQTT client", esp_ret);
       ESP_ERROR_CHECK(esp_mqtt_client_destroy(mqtt_client));
       abort();
   }

   // Wait for MQTT client to connect (blocking)
   xEventGroupWaitBits(s_mqtt_event_group, 
                       MQTT_CONNECTED_BIT, 
                       pdTRUE, 
                       pdTRUE, 
                       portMAX_DELAY);
    ESP_LOGI(TAG, "Connected");

   // Main loop
   while (1) {

       // Publish message to MQTT broker
       ESP_LOGI(TAG, "Publishing message: %s", MQTT_MSG);
       msg_id = esp_mqtt_client_publish(mqtt_client, 
                                        MQTT_PUB_TOPIC, 
                                        MQTT_MSG, 
                                        0,              // Length (0 = auto detect)
                                        MQTT_PUB_QOS,   // QoS
                                        0);             // Retain
       if (msg_id < 0) {
           ESP_LOGE(TAG, "Error (%d): Failed to publish message", msg_id);
       }

       // Wait before publishing another message
       vTaskDelay(sleep_time_ms / portTICK_PERIOD_MS);
   }
}