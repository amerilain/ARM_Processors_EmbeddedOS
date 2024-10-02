#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include "hardware/timer.h"

extern "C" {
uint32_t read_runtime_ctr(void) {
    return timer_hw->timerawl;
}
}

// Define event bits
#define TASK1_BIT (1 << 0)
#define TASK2_BIT (1 << 1)
#define TASK3_BIT (1 << 2)

// Define button pins
#define SW0_PIN 9    // Button pin for Task 1
#define SW1_PIN 8    // Button pin for Task 2
#define SW2_PIN 7   // Button pin for Task 3
#define LED_PIN 22   // D0 LED pin (if needed)

// Priorities
#define DEBUG_TASK_PRIORITY (tskIDLE_PRIORITY + 1)
#define TASK_PRIORITY (DEBUG_TASK_PRIORITY + 1)

// Event group and queue handles
EventGroupHandle_t eventGroup;
QueueHandle_t syslog_q;

// Structure for debug messages
struct debugEvent {
    const char *format;
    uint32_t data[3];
};

// Debug function to send formatted messages to the queue
void debug(const char *format, uint32_t d1, uint32_t d2, uint32_t d3) {
    struct debugEvent e;
    e.format = format;
    e.data[0] = d1;
    e.data[1] = d2;
    e.data[2] = d3;

    // Send the event to the queue
    xQueueSend(syslog_q, &e, portMAX_DELAY);
}

// Debug Task: Reads from the queue and prints the debug message
void debugTask(void *pvParameters) {
    char buffer[64];
    struct debugEvent e;

    while (1) {
        // Receive the event from the queue
        if (xQueueReceive(syslog_q, &e, portMAX_DELAY) == pdPASS) {
            snprintf(buffer, 64, e.format, e.data[0], e.data[1], e.data[2]);
            printf("%u: %s", xTaskGetTickCount(), buffer);
        }
    }
}

bool debounceButton(uint pin) {
    if (!gpio_get(pin)) {  // Active low button press detected
        vTaskDelay(pdMS_TO_TICKS(20));  // Short delay to debounce
        if (!gpio_get(pin)) {  // Check again after debounce delay
            return true;  // Button press is valid
        }
    }
    return false;  // No valid press detected
}

// Task 1-3: monitor button presses
void buttonTask(void *pvParameters) {
    uint pin = (uint)pvParameters;
    uint32_t taskBit;
    uint32_t taskNumber;

    if (pin == SW0_PIN) {
        taskBit = TASK1_BIT;
        taskNumber = 1;
    } else if (pin == SW1_PIN) {
        taskBit = TASK2_BIT;
        taskNumber = 2;
    } else if (pin == SW2_PIN) {
        taskBit = TASK3_BIT;
        taskNumber = 3;
    } else {
        // Invalid pin
        vTaskDelete(NULL);
    }

    while (1) {
        // Wait for a debounced button press
        if (debounceButton(pin)) {
            // Wait for button release (holding the button blocks the loop)
            while (!gpio_get(pin)) {
                vTaskDelay(pdMS_TO_TICKS(10));  // Wait for button release
            }

            // Set event bit for the watchdog task
            xEventGroupSetBits(eventGroup, taskBit);

            // Send debug message
            debug("Task %u: Button pressed and released.\n", taskNumber, 0, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // Polling delay
    }
}

void watchdogTask(void *pvParameters) {
    const EventBits_t bitsToWaitFor = TASK1_BIT | TASK2_BIT | TASK3_BIT;
    TickType_t lastOkTick = xTaskGetTickCount();

    while (1) {
        // Wait for all bits to be set or timeout after 30 seconds
        EventBits_t uxBits = xEventGroupWaitBits(
                eventGroup,
                bitsToWaitFor,
                pdTRUE,     // Clear bits on exit
                pdTRUE,     // Wait for all bits
                pdMS_TO_TICKS(30000)  // 30 seconds timeout
        );

        if ((uxBits & bitsToWaitFor) == bitsToWaitFor) {
            // All bits were set
            TickType_t now = xTaskGetTickCount();
            debug("OK. Elapsed ticks since last OK: %u\n", now - lastOkTick, 0, 0);
            lastOkTick = now;
        } else {
            // Timeout occurred
            EventBits_t missingBits = bitsToWaitFor & (~uxBits);
            uint32_t missingTaskNumbers[3] = {0, 0, 0};
            int missingCount = 0;
            if (missingBits & TASK1_BIT) {
                missingTaskNumbers[missingCount++] = 1;
            }
            if (missingBits & TASK2_BIT) {
                missingTaskNumbers[missingCount++] = 2;
            }
            if (missingBits & TASK3_BIT) {
                missingTaskNumbers[missingCount++] = 3;
            }

            // Send appropriate debug message
            if (missingCount == 1) {
                debug("Fail. Task not meeting deadline: %u\n", missingTaskNumbers[0], 0, 0);
            } else if (missingCount == 2) {
                debug("Fail. Tasks not meeting deadline: %u %u\n", missingTaskNumbers[0], missingTaskNumbers[1], 0);
            } else if (missingCount == 3) {
                debug("Fail. Tasks not meeting deadline: %u %u %u\n", missingTaskNumbers[0], missingTaskNumbers[1], missingTaskNumbers[2]);
            } else {
                // Should not happen
                debug("Fail. No tasks missing?\n", 0, 0, 0);
            }

            // Suspend self
            vTaskSuspend(NULL);
        }
    }
}

// Initialize GPIO Pins
void init_pins(void) {
    // Initialize tactile switches
    gpio_init(SW0_PIN);
    gpio_set_dir(SW0_PIN, GPIO_IN);
    gpio_pull_up(SW0_PIN);  // Active Low

    gpio_init(SW1_PIN);
    gpio_set_dir(SW1_PIN, GPIO_IN);
    gpio_pull_up(SW1_PIN);  // Active Low

    gpio_init(SW2_PIN);
    gpio_set_dir(SW2_PIN, GPIO_IN);
    gpio_pull_up(SW2_PIN);  // Active Low

    // Initialize LED pin (if needed)
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
}

// Main Function
int main(void) {
    // Initialize the standard I/O and pins
    stdio_init_all();  // Initialize UART for serial communication
    init_pins();       // Initialize GPIO pins

    // Initialize event group and queue
    eventGroup = xEventGroupCreate();
    syslog_q = xQueueCreate(10, sizeof(struct debugEvent));

    // Send initialization message via debug task
    debug("System Initialized\n", 0, 0, 0);

    // Create tasks
    xTaskCreate(buttonTask, "Button Task 1", 1000, (void *)SW0_PIN, TASK_PRIORITY, NULL);
    xTaskCreate(buttonTask, "Button Task 2", 1000, (void *)SW1_PIN, TASK_PRIORITY, NULL);
    xTaskCreate(buttonTask, "Button Task 3", 1000, (void *)SW2_PIN, TASK_PRIORITY, NULL);
    xTaskCreate(watchdogTask, "Watchdog Task", 1000, NULL, TASK_PRIORITY, NULL);
    xTaskCreate(debugTask, "Debug Task", 1000, NULL, DEBUG_TASK_PRIORITY, NULL);

    // Start the scheduler
    vTaskStartScheduler();

    // This point should never be reached
    for (;;);
}