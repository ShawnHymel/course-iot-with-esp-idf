#include <stdio.h>

#include "freertos/FreeRTOS.h"

// Sleep time
static const uint32_t sleep_time_ms = 1000;

void app_main(void)
{
    // Repeatedly say hello and delay
    while (1) {
        printf("Hello world!\n");
        vTaskDelay(sleep_time_ms / portTICK_PERIOD_MS); 
    }
}
