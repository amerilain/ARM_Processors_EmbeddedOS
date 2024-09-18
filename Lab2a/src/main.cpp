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

// Define the LED pin
#define LED_PIN 21
#define UART_TIMEOUT 10000       // Microseconds
#define DEBOUNCE_DELAY_MS 200    // Time to wait after the last character before blinking

// Binary semaphore handle
SemaphoreHandle_t xBinarySemaphore;

// Task to read serial input and give semaphore to blinker task
void vSerialTask(void *pvParameters) {
    int c;
    while (1) {
        c = getchar_timeout_us(UART_TIMEOUT);  // Get character with timeout
        if (c != PICO_ERROR_TIMEOUT) {
            putchar(c);  // Echo the character back to the serial port
            xSemaphoreGive(xBinarySemaphore);  // Notify blinker task
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // Small delay to prevent busy loop
    }
}

// Task to blink the LED once after the last character is received
void vBlinkTask(void *pvParameters) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    TickType_t lastBlinkTime = 0;
    const TickType_t debounceDelay = pdMS_TO_TICKS(DEBOUNCE_DELAY_MS);

    while (1) {
        // Wait for semaphore
        if (xSemaphoreTake(xBinarySemaphore, portMAX_DELAY)) {
            // Record the time of the last character received
            lastBlinkTime = xTaskGetTickCount();

            // Wait for debounce period to ensure no more characters are coming
            while ((xTaskGetTickCount() - lastBlinkTime) < debounceDelay) {
                // Check if another character has been received
                if (xSemaphoreTake(xBinarySemaphore, pdMS_TO_TICKS(DEBOUNCE_DELAY_MS))) {
                    // Update the lastBlinkTime if another character is received
                    lastBlinkTime = xTaskGetTickCount();
                } else {
                    break;  // Exit the loop if no more characters are received
                }
            }

            // Perform the blink once after the last character
            gpio_put(LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(100));  // 100 ms on
            gpio_put(LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(100));  // 100 ms off
        }
    }
}

int main() {
    // Initialize stdio (required for serial port)
    stdio_init_all();

    // Create binary semaphore
    xBinarySemaphore = xSemaphoreCreateBinary();

    // Check if semaphore was created successfully
    if (xBinarySemaphore == NULL) {
        printf("Failed to create binary semaphore.\n");
        while (1);
    }

    // Create tasks
    xTaskCreate(vSerialTask, "SerialTask", 256, NULL, 1, NULL);
    xTaskCreate(vBlinkTask, "BlinkTask", 256, NULL, 1, NULL);

    // Start the scheduler
    vTaskStartScheduler();

    // Should never reach here
    while (1);
}