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

#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/esp_debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#ifdef CONFIG_MBEDTLS_SSL_PROTO_TLS1_3
# include "psa/crypto.h"
#endif
#include "esp_crt_bundle.h"

#include "network_wrapper.h"

// Server settings and URL to fetch
#define WEB_HOST "www.howsmyssl.com"
#define WEB_PORT "443"
#define WEB_PATH "https://www.howsmyssl.com/a/check"

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

// Wait for network connection (with IP address)
static bool wait_for_network(EventGroupHandle_t network_event_group)
{
    EventBits_t network_event_bits;

    // Wait for network to connect
    ESP_LOGI(TAG, "Waiting for WiFi to connect...");
    network_event_bits = xEventGroupWaitBits(network_event_group, 
                                             NETWORK_CONNECTED_BIT, 
                                             pdFALSE, 
                                             pdTRUE, 
                                             CONNECTION_TIMEOUT_SEC * 1000 / 
                                                portTICK_PERIOD_MS);
    if (network_event_bits & NETWORK_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi");
    } else {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        return false;
    }

    // Wait for IP address
    ESP_LOGI(TAG, "Waiting for IP address...");
    network_event_bits = xEventGroupWaitBits(network_event_group, 
                                             NETWORK_IPV4_OBTAINED_BIT | 
                                             NETWORK_IPV6_OBTAINED_BIT, 
                                             pdFALSE, 
                                             pdTRUE, 
                                             CONNECTION_TIMEOUT_SEC * 1000 / 
                                                portTICK_PERIOD_MS);
    if (network_event_bits & NETWORK_IPV4_OBTAINED_BIT) {
        ESP_LOGI(TAG, "Connected to IPv4 network");
    } else if (network_event_bits & NETWORK_IPV6_OBTAINED_BIT) {
        ESP_LOGI(TAG, "Connected to IPv6 network");
    } else {
        ESP_LOGE(TAG, "Failed to obtain IP address");
        return false;
    }

    return true;
}

// Initialize MbedTLS (network wrapper, SSL/TLS, CA certificate, PRNG)
static void https_get_task(void *pvParameters)
{
    esp_err_t esp_ret;
    int tls_ret;
    int flags;
    size_t bytes_written;
    int bytes_read;
    char buf[512];
    
    
    mbedtls_entropy_context entropy_ctx;
    mbedtls_ctr_drbg_context ctr_drbg_ctx;
    mbedtls_ssl_context ssl_ctx;
    mbedtls_x509_crt ca_cert;
    mbedtls_ssl_config ssl_cfg;
    mbedtls_net_context server_fd;

#ifdef CONFIG_MBEDTLS_SSL_PROTO_TLS1_3
    // Initialize Platform Security Architecture (PSA) Crypto for Mbed TLS
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "Error (%d): Failed to initialize PSA crypto", (int)status);
        return;
    }
#endif

    // Log that we are initializing Mbed TLS
    ESP_LOGI(TAG, "Initializing MbedTLS...");

    // Initialize socket wrapper (for TCP/IP comms), acts like a file descriptor
    mbedtls_net_init(&server_fd);

    // Initialize context to hold TLS session state and data
    mbedtls_ssl_init(&ssl_ctx);

    // Initialize TLS configuration data
    mbedtls_ssl_config_init(&ssl_cfg);

    // Initialize struct to load root CA certificates (for server verification)
    mbedtls_x509_crt_init(&ca_cert);
    
    // Initialize context for CTR_DRBG (cryptographically secure PRNG)
    mbedtls_ctr_drbg_init(&ctr_drbg_ctx);
    
    // Initialize entropy source
    mbedtls_entropy_init(&entropy_ctx);
    tls_ret = mbedtls_ctr_drbg_seed(&ctr_drbg_ctx,
                                mbedtls_entropy_func,
                                &entropy_ctx,
                                NULL,
                                0);
    if (tls_ret != 0) {
        ESP_LOGE(TAG, "Error (%d): Failed to seed CTR-DRBG RNG", tls_ret);
        return;
    }

#ifdef CONFIG_MBEDTLS_DEBUG
    // Enable Mbed TLS debug output
    mbedtls_esp_enable_debug_log(&ssl_cfg, CONFIG_MBEDTLS_DEBUG_LEVEL);
#endif

    // Attach default ESP-IDF trust store (CA certificates) to SSL configuration
    esp_ret = esp_crt_bundle_attach(&ssl_cfg);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (%d): Failed to attach CA certificates", esp_ret);
        return;
    }

    // Hostname should match Common Name (CN) in server certificate
    tls_ret = mbedtls_ssl_set_hostname(&ssl_ctx, WEB_HOST);
    if (tls_ret != 0) {
        ESP_LOGE(TAG, 
                 "Error (%d): Failed to set hostname for TLS session",
                 tls_ret);
        return;
    }

    // Configure TLS for client
    tls_ret = mbedtls_ssl_config_defaults(&ssl_cfg,
                                          MBEDTLS_SSL_IS_CLIENT,        // Client mode
                                          MBEDTLS_SSL_TRANSPORT_STREAM, // TLS (not DTLS)
                                          MBEDTLS_SSL_PRESET_DEFAULT);  // Default security settings
    if (tls_ret != 0) {
        ESP_LOGE(TAG, "Error (%d): Failed to set TLS configuration", tls_ret);
        return;
    }

    // Require mutual authentication (server must present a certificate)
    mbedtls_ssl_conf_authmode(&ssl_cfg, MBEDTLS_SSL_VERIFY_REQUIRED);

    // Set up certificate aurhtority (CA) chain (with no revocation list)
    mbedtls_ssl_conf_ca_chain(&ssl_cfg, &ca_cert, NULL);

    // Set random number generator (RNG) callback function
    mbedtls_ssl_conf_rng(&ssl_cfg, mbedtls_ctr_drbg_random, &ctr_drbg_ctx);

    // Set send and receive callback functions basic I/O operations
    mbedtls_ssl_set_bio(&ssl_ctx,
                        &server_fd,
                        mbedtls_net_send,
                        mbedtls_net_recv,
                        NULL);

    // Assign the TLS configuration to the TLS context
    tls_ret = mbedtls_ssl_setup(&ssl_ctx, &ssl_cfg);
    if (tls_ret != 0) {
        ESP_LOGE(TAG, "Error (%d): Failed to set up TLS context", tls_ret);
        return;
    }

    // %%%TEST - task loop
    while(1) {

        // Connect to server using hostname and port over TCP
        ESP_LOGI(TAG, "Connecting to %s:%s...", WEB_HOST, WEB_PORT);
        tls_ret = mbedtls_net_connect(&server_fd, 
                                      WEB_HOST, 
                                      WEB_PORT, 
                                      MBEDTLS_NET_PROTO_TCP);
        if (tls_ret != 0) {
            ESP_LOGE(TAG, "Error (%d): Failed to connect to server", tls_ret);
            goto exit;
        }
        ESP_LOGI(TAG, "Connected");

        // Perform SSL/TLS handshake
        // %%%TODO: Fix blocking call, call mbedtls_ssl_free() on failure
        ESP_LOGI(TAG, "Performing SSL/TLS handshake...");
        do {
            tls_ret = mbedtls_ssl_handshake(&ssl_ctx);
            if ((tls_ret != 0) && 
                (tls_ret != MBEDTLS_ERR_SSL_WANT_READ) && 
                (tls_ret != MBEDTLS_ERR_SSL_WANT_WRITE)) {
                ESP_LOGE(TAG, 
                         "Error (%d): Failed to perform SSL/TLS handshake",
                         tls_ret);
                goto exit;
            }
        } while (tls_ret != 0);
        ESP_LOGI(TAG, "Handshake complete");

        // Verify server certificate
        ESP_LOGI(TAG, "Verifying peer X.509 certificate...");
        flags = mbedtls_ssl_get_verify_result(&ssl_ctx);
        if (flags != 0) {
            ESP_LOGW(TAG, "Failed to verify peer certificate");
            memset(buf, 0, sizeof(buf));
            mbedtls_x509_crt_verify_info(buf, sizeof(buf), "  ! ", flags);
            ESP_LOGW(TAG, "Certificate verification info: %s", buf);
        } else {
            ESP_LOGI(TAG, "Certificate verified");
        }

        // Print cipher suite
        ESP_LOGI(TAG, 
                 "Cipher suite is %s", 
                 mbedtls_ssl_get_ciphersuite(&ssl_ctx));

        // Write HTTP request (potential for multiple partial writes)
        ESP_LOGI(TAG, "Writing HTTP request...");
        bytes_written = 0;
        do {
            tls_ret = mbedtls_ssl_write(&ssl_ctx,
                                        (const unsigned char *)REQUEST + 
                                        bytes_written,
                                        strlen(REQUEST) - bytes_written);
            if (tls_ret >= 0) {
                ESP_LOGI(TAG, "%d bytes written", tls_ret);
                bytes_written += tls_ret;
            } else if ((tls_ret != MBEDTLS_ERR_SSL_WANT_WRITE) && 
                       (tls_ret != MBEDTLS_ERR_SSL_WANT_READ)) {
                ESP_LOGE(TAG, 
                         "Error (%d): Failed to write HTTP request",
                         tls_ret);
                goto exit;
            }
        } while(bytes_written < strlen(REQUEST));

        // Read HTTP response (potential for multiple partial reads)
        ESP_LOGI(TAG, "Reading HTTP response...");
        do {
            bytes_read = sizeof(buf) - 1;
            memset(buf, 0, sizeof(buf));
            tls_ret = mbedtls_ssl_read(&ssl_ctx, 
                                       (unsigned char *)buf,
                                       bytes_read);

#if CONFIG_MBEDTLS_SSL_PROTO_TLS1_3 && CONFIG_MBEDTLS_CLIENT_SSL_SESSION_TICKETS
            // In TLS 1.3, session tickets are received as a separate message
            if (tls_ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
                ESP_LOGD(TAG, "Received session ticket in TLS 1.3, retry read");
                continue;
            }
#endif

            // Incomplete handshake, retry read
            if (tls_ret == MBEDTLS_ERR_SSL_WANT_READ || tls_ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }

            // If server closed connection, break loop
            if (tls_ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                tls_ret = 0;
                break;
            }

            // Connection was closed without notification, end session
            if (tls_ret == 0) {
                ESP_LOGI(TAG, "Connection closed");
                break;
            }

            // Other errors, end session
            if (tls_ret < 0) {
                ESP_LOGE(TAG, 
                         "Error (%d): Failed to read HTTP response",
                         tls_ret);
                break;
            }

            // Print response directly to console as buffer is read
            // %%%TEST: Find a better way to abstract this
            bytes_read = tls_ret;
            ESP_LOGD(TAG, "%d bytes read", bytes_read);
            for (int i = 0; i < bytes_read; i++) {
                putchar(buf[i]);
            }
        } while (1);

        // Notify server that we're closing the connection
        mbedtls_ssl_close_notify(&ssl_ctx);

    exit:

        // %%%TODO: close(server_fd)???

        // %%%TODO: Free context instead of resetting???
        //  mbedtls_ssl_free(&ssl_ctx);
        //  mbedtls_x509_crt_free(&ca_cert);
        //  mbedtls_ssl_config_free(&ssl_cfg);
        //  mbedtls_net_free(&server_fd);
        //  mbedtls_ctr_drbg_free(&ctr_drbg_ctx);
        //  mbedtls_entropy_free(&entropy_ctx);
        mbedtls_ssl_session_reset(&ssl_ctx);
        mbedtls_net_free(&server_fd);

        // %%%TEST
        printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

// Main app entrypoint
void app_main(void)
{
    esp_err_t esp_ret;
    EventGroupHandle_t network_event_group;

    // Welcome message (after delay to allow serial connection)
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Starting HTTPS GET request demo");

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

    // Make sure network is connected and has an IP address
    if (!wait_for_network(network_event_group)) {
        ESP_LOGE(TAG, "Failed to connect to WiFi. Reconnecting...");
        esp_ret = network_reconnect();
        if (esp_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reconnect WiFi (%d)", esp_ret);
        }
        return;
    }

    // %%%TEST - new task. Note lots of memory for stack!
    xTaskCreate(&https_get_task, "https_get_task", 8192, NULL, 5, NULL);
}