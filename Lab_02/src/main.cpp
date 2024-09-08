#include <cstdio>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

extern "C" {
uint32_t read_runtime_ctr(void) {
    return timer_hw->timerawl;  // Use hardware timer for runtime stats
    }
}

#define LED_PIN 21
#define ROT_A_PIN 10
#define ROT_B_PIN 11
#define ROT_SW_PIN 12
#define UART_TIMEOUT 10000 // microseconds
#define DEBOUNCE_DELAY_MS 500  // 500 ms debounce window
#define BUTTON_DEBOUNCE_MS 250 // 250 ms debounce for button
#define MIN_FREQUENCY 2        // 2 Hz minimum
#define MAX_FREQUENCY 200      // 200 Hz maximum

SemaphoreHandle_t xBinarySemaphore;
TickType_t lastReceivedTime = 0;
TickType_t lastButtonPress = 0;  // Last button press time
int ledState = 0;                // 0 = off, 1 = on
int blinkFrequency = 5;          // Initial blink frequency (in Hz)

// Task 1: Reads serial input and gives semaphore to blinker task
void vSerialTask(void *pvParameters) {
    char c;
    while (1) {
        c = getchar_timeout_us(UART_TIMEOUT);  // Get character with timeout
        if (c != PICO_ERROR_TIMEOUT && c >= 32 && c <= 126) {
            printf("Received character: %c\n", c);
            lastReceivedTime = xTaskGetTickCount();  // Update the last received time
            xSemaphoreGive(xBinarySemaphore);  // Notify blinker task
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // Small delay to prevent busy loop
    }
}

// Task 2: Blinks LED after receiving semaphore, ensuring one blink after the last character
void vBlinkTask(void *pvParameters) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    while (1) {
        if (xSemaphoreTake(xBinarySemaphore, portMAX_DELAY)) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_DELAY_MS));
            if ((xTaskGetTickCount() - lastReceivedTime) >= pdMS_TO_TICKS(DEBOUNCE_DELAY_MS)) {
                if (ledState) {
                    gpio_put(LED_PIN, 1);
                    vTaskDelay(pdMS_TO_TICKS(500 / blinkFrequency));
                    gpio_put(LED_PIN, 0);
                    vTaskDelay(pdMS_TO_TICKS(500 / blinkFrequency));
                }
            }
        }
    }
}

// GPIO interrupt handler for rotary encoder and button
void gpio_callback(uint gpio, uint32_t events) {
    TickType_t now = xTaskGetTickCount();

    if (gpio == ROT_SW_PIN && events == GPIO_IRQ_EDGE_FALL) {
        // Debounce button press
        if ((now - lastButtonPress) > pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
            ledState = !ledState;  // Toggle LED state
            printf("LED state: %d\n", ledState);
            lastButtonPress = now;
        }
    } else if (gpio == ROT_A_PIN && (events == GPIO_IRQ_EDGE_RISE)) {
        // Handle rotary encoder turning
        if (ledState) {  // Only adjust frequency if the LED is ON
            if (gpio_get(ROT_B_PIN) == 0) {
                // Clockwise turn (increase frequency)
                blinkFrequency = (blinkFrequency < MAX_FREQUENCY) ? blinkFrequency + 1 : MAX_FREQUENCY;
            } else {
                // Counterclockwise turn (decrease frequency)
                blinkFrequency = (blinkFrequency > MIN_FREQUENCY) ? blinkFrequency - 1 : MIN_FREQUENCY;
            }
            printf("Blink frequency: %d Hz\n", blinkFrequency);
        }
    }
}

// GPIO setup for rotary encoder and button
void setup_gpio() {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);  // Ensure LED is off initially

    // Initialize rotary encoder pins
    gpio_init(ROT_A_PIN);
    gpio_init(ROT_B_PIN);
    gpio_set_dir(ROT_A_PIN, GPIO_IN);
    gpio_set_dir(ROT_B_PIN, GPIO_IN);

    // Initialize push-button
    gpio_init(ROT_SW_PIN);
    gpio_set_dir(ROT_SW_PIN, GPIO_IN);
    gpio_pull_up(ROT_SW_PIN);

    // Set up GPIO interrupts
    gpio_set_irq_enabled_with_callback(ROT_A_PIN, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
    gpio_set_irq_enabled_with_callback(ROT_SW_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
}

int main() {
    stdio_init_all();

    // Setup GPIOs
    setup_gpio();

    // Create binary semaphore
    xBinarySemaphore = xSemaphoreCreateBinary();

    // Create tasks
    xTaskCreate(vSerialTask, "Serial Task", 256, NULL, 1, NULL);
    xTaskCreate(vBlinkTask, "Blink Task", 256, NULL, 1, NULL);

    // Start scheduler
    vTaskStartScheduler();

    while(1);
}