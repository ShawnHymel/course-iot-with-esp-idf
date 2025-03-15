/**
 * SPDX-FileCopyrightText: 2025 Shawn Hymel
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_crt_bundle.h"
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

#include "network_wrapper.h"

// Settings
static const uint32_t sleep_time_ms = 5000;

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
#define CONNECTION_TIMEOUT_SEC  10  // Set delay to wait for connection (sec)

// Tag for debug messages
static const char *TAG = "https_request";

// Static globals
static mbedtls_net_context s_net_ctx;
static mbedtls_ssl_context s_ssl_ctx;
static mbedtls_ssl_config s_ssl_cfg;
static mbedtls_x509_crt s_ca_cert;
static mbedtls_entropy_context s_entropy_ctx;
static mbedtls_ctr_drbg_context s_ctr_drbg_ctx;

/*******************************************************************************
 * Private function prototypes
 */

static esp_err_t tls_init();
static void tls_deinit();
static esp_err_t https_get();

/*******************************************************************************
 * Private function definitions
 */

// Initialize Mbed TLS ()
static esp_err_t tls_init()
{
    esp_err_t esp_ret = ESP_FAIL;
    int tls_ret;

#ifdef CONFIG_MBEDTLS_SSL_PROTO_TLS1_3
    psa_status_t psa_status;

    // Initialize Platform Security Architecture (PSA) Crypto for Mbed TLS
    psa_status = psa_crypto_init();
    if (psa_status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "Error (%d): Failed to initialize PSA crypto", (int)psa_status);
        return ESP_FAIL;
    }
#endif

    // Log that we are initializing Mbed TLS
    ESP_LOGI(TAG, "Initializing MbedTLS...");

    // Initialize contexts
    mbedtls_net_init(&s_net_ctx);           // Socket wrapper (like file descriptor)
    mbedtls_ssl_init(&s_ssl_ctx);           // Holds TLS session state and data
    mbedtls_ssl_config_init(&s_ssl_cfg);    // TLS configuration
    mbedtls_x509_crt_init(&s_ca_cert);      // Holds root CA certificates
    mbedtls_ctr_drbg_init(&s_ctr_drbg_ctx); // Cryptographically secure PRNG
    mbedtls_entropy_init(&s_entropy_ctx);   // Entropy context
    
    // Seed pseudorandom number generator
    tls_ret = mbedtls_ctr_drbg_seed(&s_ctr_drbg_ctx,
                                mbedtls_entropy_func,
                                &s_entropy_ctx,
                                NULL,
                                0);
    if (tls_ret != 0) {
        ESP_LOGE(TAG, "Error (%d): Failed to seed CTR-DRBG RNG", tls_ret);
        goto cleanup;
    }

#ifdef CONFIG_MBEDTLS_DEBUG
    // Enable Mbed TLS debug output
    mbedtls_esp_enable_debug_log(&s_ssl_cfg, CONFIG_MBEDTLS_DEBUG_LEVEL);
#endif

    // Attach default ESP-IDF trust store (CA certificates) to SSL configuration
    esp_ret = esp_crt_bundle_attach(&s_ssl_cfg);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (%d): Failed to attach CA certificates", esp_ret);
        goto cleanup;
    }

    // Hostname should match Common Name (CN) in server certificate
    tls_ret = mbedtls_ssl_set_hostname(&s_ssl_ctx, WEB_HOST);
    if (tls_ret != 0) {
        ESP_LOGE(TAG, "Error (%d): Failed to set hostname for TLS session", tls_ret);
        goto cleanup;
    }

    // Configure TLS for client
    tls_ret = mbedtls_ssl_config_defaults(&s_ssl_cfg,
                                          MBEDTLS_SSL_IS_CLIENT,        // Client mode
                                          MBEDTLS_SSL_TRANSPORT_STREAM, // TLS (not DTLS)
                                          MBEDTLS_SSL_PRESET_DEFAULT);  // Default security settings
    if (tls_ret != 0) {
        ESP_LOGE(TAG, "Error (%d): Failed to set TLS configuration", tls_ret);
        goto cleanup;
    }

    // Require authentication (server must present a certificate)
    mbedtls_ssl_conf_authmode(&s_ssl_cfg, MBEDTLS_SSL_VERIFY_REQUIRED);

    // Set up certificate aurhtority (CA) chain (with no revocation list)
    mbedtls_ssl_conf_ca_chain(&s_ssl_cfg, &s_ca_cert, NULL);

    // Set random number generator (RNG) callback function
    mbedtls_ssl_conf_rng(&s_ssl_cfg, mbedtls_ctr_drbg_random, &s_ctr_drbg_ctx);

    // Set send and receive callback functions basic I/O operations
    mbedtls_ssl_set_bio(&s_ssl_ctx,
                        &s_net_ctx,
                        mbedtls_net_send,
                        mbedtls_net_recv,
                        NULL);

    // Set up TLS context
    tls_ret = mbedtls_ssl_setup(&s_ssl_ctx, &s_ssl_cfg);
    if (tls_ret != 0) {
        ESP_LOGE(TAG, "Error (%d): Failed to set up TLS context", tls_ret);
        goto cleanup;
    }

    // Set success return value
    esp_ret = ESP_OK;

cleanup:
    // Free all initialized resources in reverse order of initialization if not successful
    if (esp_ret != ESP_OK) {
        tls_deinit();
    }

    return esp_ret;
}

// Free all TLS resources
static void tls_deinit()
{
    mbedtls_ssl_free(&s_ssl_ctx);
    mbedtls_ssl_config_free(&s_ssl_cfg);
    mbedtls_x509_crt_free(&s_ca_cert);
    mbedtls_ctr_drbg_free(&s_ctr_drbg_ctx);
    mbedtls_entropy_free(&s_entropy_ctx);
    mbedtls_net_free(&s_net_ctx);
}

// Initialize MbedTLS (network wrapper, SSL/TLS, CA certificate, PRNG)
static esp_err_t https_get()
{
    esp_err_t esp_ret = ESP_FAIL;
    int tls_ret;
    int flags;
    size_t bytes_written;
    int bytes_read;
    char buf[512];

    // Connect to server using hostname and port over TCP
    ESP_LOGI(TAG, "Connecting to %s:%s...", WEB_HOST, WEB_PORT);
    tls_ret = mbedtls_net_connect(&s_net_ctx, 
                                  WEB_HOST, 
                                  WEB_PORT, 
                                  MBEDTLS_NET_PROTO_TCP);
    if (tls_ret != 0) {
        ESP_LOGE(TAG, "Error (%d): Failed to connect to server", tls_ret);
        goto cleanup;
    }
    ESP_LOGI(TAG, "Connected");

    // Perform SSL/TLS handshake (note: blocking)
    ESP_LOGI(TAG, "Performing SSL/TLS handshake...");
    do {
        tls_ret = mbedtls_ssl_handshake(&s_ssl_ctx);
        if ((tls_ret != 0) && 
            (tls_ret != MBEDTLS_ERR_SSL_WANT_READ) && 
            (tls_ret != MBEDTLS_ERR_SSL_WANT_WRITE)) {
            ESP_LOGE(TAG, 
                        "Error (%d): Failed to perform SSL/TLS handshake",
                        tls_ret);
            goto cleanup;
        }
    } while (tls_ret != 0);
    ESP_LOGI(TAG, "Handshake complete");

    // Verify server certificate
    ESP_LOGI(TAG, "Verifying peer X.509 certificate...");
    flags = mbedtls_ssl_get_verify_result(&s_ssl_ctx);
    if (flags != 0) {
        ESP_LOGW(TAG, "Failed to verify peer certificate");
        memset(buf, 0, sizeof(buf));
        mbedtls_x509_crt_verify_info(buf, sizeof(buf), "  ! ", flags);
        ESP_LOGW(TAG, "Certificate verification info: %s", buf);
    } else {
        ESP_LOGI(TAG, "Certificate verified");
    }

    // Print cipher suite
    ESP_LOGI(TAG, "Cipher suite is %s", mbedtls_ssl_get_ciphersuite(&s_ssl_ctx));

    // Write HTTP request (potential for multiple partial writes)
    ESP_LOGI(TAG, "Writing HTTP request...");
    bytes_written = 0;
    do {
        tls_ret = mbedtls_ssl_write(&s_ssl_ctx,
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
            goto cleanup;
        }
    } while(bytes_written < strlen(REQUEST));

    // Print HTTP response (potential for multiple partial reads)
    ESP_LOGI(TAG, "Reading HTTP response...");
    do {
        bytes_read = sizeof(buf) - 1;
        memset(buf, 0, sizeof(buf));
        tls_ret = mbedtls_ssl_read(&s_ssl_ctx, 
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
            ESP_LOGE(TAG, "Error (%d): Failed to read HTTP response", tls_ret);
            break;
        }

        // Print response directly to console
        bytes_read = tls_ret;
        buf[bytes_read] = '\0';
        ESP_LOGD(TAG, "%d bytes read", bytes_read);
        printf("%s", buf);

    } while (1);

    // Set return value
    if (tls_ret == 0) {
        esp_ret = ESP_OK;
    } else {
        esp_ret = ESP_FAIL;
    }

cleanup:
    // Notify server that we're closing the connection
    mbedtls_ssl_close_notify(&s_ssl_ctx);

    // Reset the TLS context
    mbedtls_ssl_session_reset(&s_ssl_ctx);

    // Free the network context
    mbedtls_net_free(&s_net_ctx);

    return esp_ret;
}

/*******************************************************************************
 * Main entrypoint
 */

void app_main(void)
{
    esp_err_t esp_ret;
    EventGroupHandle_t network_event_group;
    EventBits_t network_event_bits;

    // Initialize event group
    network_event_group = xEventGroupCreate();

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

    // Superloop
    while(1) {

        // Initialize TLS
        esp_ret = tls_init();
        if (esp_ret != ESP_OK) {
            ESP_LOGE(TAG, "Error (%d): Failed to initialize Mbed TLS", esp_ret);
            abort();
        }

        // Do as long as we have a successful TLS connection
        while(1) {

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

            // Perform HTTPS GET request and print response to terminal
            esp_ret = https_get();
            if (esp_ret != ESP_OK) {
                ESP_LOGE(TAG, "Error (%d): HTTPS GET request failed", esp_ret);
                tls_deinit();
                break;
            }

            // Print amount of free heap memory (check for memory leak)
            printf("\r\nFree heap: %lu\r\n", esp_get_free_heap_size());

            // Delay
            vTaskDelay(sleep_time_ms / portTICK_PERIOD_MS);
        }
    }
}