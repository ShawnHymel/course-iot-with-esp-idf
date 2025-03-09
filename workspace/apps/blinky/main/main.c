#include <stdio.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

// Settings
static const gpio_num_t led_pin = GPIO_NUM_4;
static const uint32_t sleep_time_ms = 1000;

void app_main(void)
{
    uint8_t led_state = 0;

    // Configure the GPIO
    gpio_reset_pin(led_pin);
    gpio_set_direction(led_pin, GPIO_MODE_OUTPUT);

    // Superloop
    while (1) {

        // Toggle the LED
        led_state = !led_state;
        gpio_set_level(led_pin, led_state);

        // Print LED state
        printf("LED state: %d\n", led_state);

#ifdef QEMU_ENV
        printf("I'm running on QEMU!\r\n");
#endif

        // Delay
        vTaskDelay(sleep_time_ms / portTICK_PERIOD_MS); 
    }
}