#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "pico/stdlib.h"
#include <cstdio>

extern "C" {
uint32_t read_runtime_ctr(void) {
    return timer_hw->timerawl;
}
}

#define BUTTON_SW0_PIN 9
#define BUTTON_SW1_PIN 8
#define BUTTON_SW2_PIN 7
#define LED_PIN 22  // D1

QueueHandle_t buttonQueue;

// Sequence to unlock the lock
const uint8_t unlockSequence[] = {0, 0, 2, 1, 2};
const size_t unlockSequenceLength = sizeof(unlockSequence) / sizeof(unlockSequence[0]);

void button_task(void *param) {
    const uint8_t buttonId = *(uint8_t *)param;
    const uint button_pin = (buttonId == 0) ? BUTTON_SW0_PIN :
                            (buttonId == 1) ? BUTTON_SW1_PIN :
                            BUTTON_SW2_PIN;

    gpio_init(button_pin);
    gpio_set_dir(button_pin, GPIO_IN);
    gpio_pull_up(button_pin);

    while (true) {
        if (!gpio_get(button_pin)) {  // Button pressed (active low)
            printf("Button %d pressed\n", buttonId);
            while (!gpio_get(button_pin)) {
                vTaskDelay(pdMS_TO_TICKS(10));  // Debounce delay
            }
            if (xQueueSend(buttonQueue, &buttonId, portMAX_DELAY) != pdPASS) {
                printf("Failed to send button press to queue\n");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // Poll every 100ms
    }
}

void sequence_task(void *param) {
    uint8_t receivedButton;
    size_t sequenceIndex = 0;
    bool sequenceStarted = false;

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);  // Set LED off initially

    while (true) {
        if (xQueueReceive(buttonQueue, &receivedButton, pdMS_TO_TICKS(5000)) == pdPASS) {
            sequenceStarted = true;  // Sequence started
            printf("Received button %d\n", receivedButton);
            if (receivedButton == unlockSequence[sequenceIndex]) {
                sequenceIndex++;
                printf("Correct button! Sequence index: %d\n", sequenceIndex);
                if (sequenceIndex == unlockSequenceLength) {
                    // Sequence completed successfully
                    printf("Sequence complete! Unlocking...\n");
                    for (int i = 0; i < 3; i++) {
                        printf("Blinking LED: %d\n", i+1);
                        gpio_put(LED_PIN, 1);
                        vTaskDelay(pdMS_TO_TICKS(200));
                        gpio_put(LED_PIN, 0);
                        vTaskDelay(pdMS_TO_TICKS(200));
                    }
                    sequenceIndex = 0;
                    sequenceStarted = false;
                }
            } else {
                printf("Wrong button! Resetting sequence.\n");
                sequenceIndex = 0;  // Reset if wrong button is pressed
                sequenceStarted = false;  // Reset sequence started flag
            }
        } else if (sequenceStarted) {
            // Timeout occurred after sequence started
            printf("Timeout! Resetting sequence.\n");
            sequenceIndex = 0;
            sequenceStarted = false;  // Reset sequence started flag
        }
    }
}

int main() {
    stdio_init_all();

    // Create the queue with space for 10 uint8_t items
    buttonQueue = xQueueCreate(10, sizeof(uint8_t));
    if (buttonQueue == NULL) {
        printf("Failed to create queue\n");
        while (1);  // stop if queue creation fails
    }

    // Define button IDs
    const uint8_t button0Id = 0;  // SW0
    const uint8_t button1Id = 1;  // SW1
    const uint8_t button2Id = 2;  // SW2

    // Create tasks for each button
    xTaskCreate(button_task, "Button_SW0", 256, (void *)&button0Id, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(button_task, "Button_SW1", 256, (void *)&button1Id, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(button_task, "Button_SW2", 256, (void *)&button2Id, tskIDLE_PRIORITY + 1, NULL);

    // Create the sequence processing task
    xTaskCreate(sequence_task, "Sequence", 256, NULL, tskIDLE_PRIORITY + 2, NULL);

    vTaskStartScheduler();

    while(1){};
}