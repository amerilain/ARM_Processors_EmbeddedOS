#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "hardware/timer.h"

uint32_t read_runtime_ctr(void) {
    return time_us_32();
}

#define LED_PIN 21
#define ROT_A_PIN 10
#define ROT_B_PIN 11
#define ROT_SW_PIN 12

#define BUTTON_DEBOUNCE_MS 250
#define MIN_FREQUENCY 2
#define MAX_FREQUENCY 200

typedef enum {
    BUTTON_PRESS,
    ENCODER_TURN_CLOCKWISE,
    ENCODER_TURN_COUNTERCLOCKWISE
} EventType;

typedef struct {
    EventType type;
    TickType_t time;
} Event;

QueueHandle_t xEventQueue;

volatile int ledState = 0;
volatile int blinkFrequency = 5;

void gpio_callback(uint gpio, uint32_t events) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    Event event;
    event.time = xTaskGetTickCountFromISR();

    if (gpio == ROT_SW_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        event.type = BUTTON_PRESS;
        xQueueSendFromISR(xEventQueue, &event, &xHigherPriorityTaskWoken);
    } else if (gpio == ROT_A_PIN && (events & GPIO_IRQ_EDGE_RISE)) {
        // Determine direction based on state of ROT_B_PIN
        if (gpio_get(ROT_B_PIN) == 0) {
            event.type = ENCODER_TURN_CLOCKWISE;
        } else {
            event.type = ENCODER_TURN_COUNTERCLOCKWISE;
        }
        xQueueSendFromISR(xEventQueue, &event, &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void vEventTask(void *pvParameters) {
    Event event;
    TickType_t lastButtonPressTime = 0;

    while (1) {
        if (xQueueReceive(xEventQueue, &event, portMAX_DELAY)) {
            switch (event.type) {
                case BUTTON_PRESS:
                    // Debounce button
                    if ((event.time - lastButtonPressTime) > pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
                        ledState = !ledState;  // Toggle LED state
                        printf("LED state: %d\nBlink frequency: %d Hz\n", ledState, blinkFrequency);
                        lastButtonPressTime = event.time;
                    }
                    break;

                case ENCODER_TURN_CLOCKWISE:
                    if (ledState) {
                        blinkFrequency = (blinkFrequency < MAX_FREQUENCY) ? blinkFrequency + 1 : MAX_FREQUENCY;
                        printf("Blink frequency: %d Hz\n", blinkFrequency);
                    }
                    break;

                case ENCODER_TURN_COUNTERCLOCKWISE:
                    if (ledState) {
                        blinkFrequency = (blinkFrequency > MIN_FREQUENCY) ? blinkFrequency - 1 : MIN_FREQUENCY;
                        printf("Blink frequency: %d Hz\n", blinkFrequency);
                    }
                    break;

                default:
                    break;
            }
        }
    }
}

void vBlinkTask(void *pvParameters) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);  // Ensure LED is off initially

    while (1) {
        if (ledState) {
            gpio_put(LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(500 / blinkFrequency));
            gpio_put(LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(500 / blinkFrequency));
        } else {
            // LED is OFF
            gpio_put(LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(100));  // Sleep for a short while
        }
    }
}

void setup_gpio() {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);  // Ensure LED is off initially

    gpio_init(ROT_A_PIN);
    gpio_init(ROT_B_PIN);
    gpio_set_dir(ROT_A_PIN, GPIO_IN);
    gpio_set_dir(ROT_B_PIN, GPIO_IN);

    gpio_init(ROT_SW_PIN);
    gpio_set_dir(ROT_SW_PIN, GPIO_IN);
    gpio_pull_up(ROT_SW_PIN);

    gpio_set_irq_enabled_with_callback(ROT_A_PIN, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
    gpio_set_irq_enabled(ROT_SW_PIN, GPIO_IRQ_EDGE_FALL, true);
}

int main() {
    stdio_init_all();

    setup_gpio();

    xEventQueue = xQueueCreate(10, sizeof(Event));
    if (xEventQueue == NULL) {
        printf("Failed to create event queue.\n");
        while (1);
    }
    vQueueAddToRegistry(xEventQueue, "EventQueue");

    xTaskCreate(vEventTask, "EventTask", 256, NULL, 1, NULL);
    xTaskCreate(vBlinkTask, "BlinkTask", 256, NULL, 1, NULL);

    vTaskStartScheduler();

    // Should never reach here
    while (1);
}