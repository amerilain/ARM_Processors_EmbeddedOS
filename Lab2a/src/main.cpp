#include <cstdio>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "hardware/timer.h"
extern "C" {
uint32_t read_runtime_ctr(void) {
    return timer_hw->timerawl;
}
}

#define LED_PIN 21
#define UART_TIMEOUT 10000
#define DEBOUNCE_DELAY_MS 200

SemaphoreHandle_t xSemaphore;

void vSerialTask(void *pvParameters) {
    int c;
    while (1) {
        c = getchar_timeout_us(UART_TIMEOUT);
        if (c != PICO_ERROR_TIMEOUT) {
            putchar(c);  // Echo character back to the serial port
            xSemaphoreGive(xSemaphore);  // give semaphore to blinker task
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void vBlinkTask(void *pvParameters) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    TickType_t lastBlinkTime = 0;
    const TickType_t debounceDelay = pdMS_TO_TICKS(DEBOUNCE_DELAY_MS);

    while (1) {
        // Wait for semaphore
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY)) {
            lastBlinkTime = xTaskGetTickCount();

            // Wait for more characters
            while ((xTaskGetTickCount() - lastBlinkTime) < debounceDelay) {
                // Check for another character
                if (xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(DEBOUNCE_DELAY_MS))) {
                    lastBlinkTime = xTaskGetTickCount();
                } else {
                    break;
                }
            }

            gpio_put(LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_put(LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

int main() {
    stdio_init_all();

    xSemaphore = xSemaphoreCreateBinary();

    if (xSemaphore == NULL) {
        printf("Failed to create binary semaphore.\n");
        while (1);
    }

    xTaskCreate(vSerialTask, "SerialTask", 256, NULL, 1, NULL);
    xTaskCreate(vBlinkTask, "BlinkTask", 256, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1);
}