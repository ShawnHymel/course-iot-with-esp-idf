#include <stdio.h>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"

// Settings
static const i2c_port_num_t i2c_port = 0;           // -1 for auto-select
static const gpio_num_t i2c_sda_pin = 5;            // GPIO number for SDA
static const gpio_num_t i2c_scl_pin = 6;            // GPIO number for SCL
static const uint8_t i2c_glitch_ignore_cnt = 7;     // 7 is typical
static const uint16_t tmp10x_addr = 0x48;           // TMP102/105 I2C address
static const uint32_t tmp10x_scl_speed_hz = 100000; // 100kHz (standard mode)
static const uint32_t sleep_time_ms = 1000;

// Constants
static const uint8_t tmp10x_reg_temp = 0x00;

void app_main(void)
{
    esp_err_t esp_ret;
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t tmp10x_dev;
    uint8_t reg;
    uint8_t data[2];
    int16_t temperature;

    // Set I2C bus configuration
    i2c_master_bus_config_t bus_config = {
        .i2c_port = i2c_port,
        .sda_io_num = i2c_sda_pin,
        .scl_io_num = i2c_scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = i2c_glitch_ignore_cnt,
        .flags.enable_internal_pullup = true,
    };

    // Initialize the I2C bus
    esp_ret = i2c_new_master_bus(&bus_config, &i2c_bus);
    if (esp_ret != ESP_OK) {
        printf("Error: Failed to initialize I2C bus\r\n");
        abort();
    }

    // Set I2C device configuration
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = tmp10x_addr,
        .scl_speed_hz = tmp10x_scl_speed_hz,
    };

    // Initialize the TMP10x I2C device on the bus
    esp_ret = i2c_master_bus_add_device(i2c_bus, &dev_config, &tmp10x_dev);
    if (esp_ret != ESP_OK) {
        printf("Error: Failed to initialize I2C device\r\n");
        abort();
    }

    // Superloop
    while (1) {

        // Delay
        vTaskDelay(sleep_time_ms / portTICK_PERIOD_MS);

        // Store register address in buffer
        reg = tmp10x_reg_temp;

        // Read temperature
        esp_ret = i2c_master_transmit_receive(tmp10x_dev, &reg, 1, data, 2, -1);
        if (esp_ret != ESP_OK) {
            printf("Error: Failed to read temperature\r\n");
            continue;
        }

        // Convert data to temperature (deg C)
        temperature = (data[0] << 8) | data[1];
        temperature >>= 4;
        temperature *= 0.0625;

        // Print temperature
        printf("Temperature: %d deg C\r\n", temperature);
    }
}