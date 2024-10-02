#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "semphr.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>

extern "C" {
uint32_t read_runtime_ctr() {
    return 0;  // Return a dummy value for now
}
}

#define BUTTON_BIT (1 << 0)  // Bit 0 for button press
#define SW1_PIN 8            // Button pin
#define LED_PIN 22           // D0 LED pin

#define DEBUG_TASK_PRIORITY (tskIDLE_PRIORITY + 1)
#define BUTTON_TASK_PRIORITY (DEBUG_TASK_PRIORITY + 1)
#define TASK2_PRIORITY (DEBUG_TASK_PRIORITY + 1)
#define TASK3_PRIORITY (DEBUG_TASK_PRIORITY + 1)

EventGroupHandle_t eventGroup;
QueueHandle_t syslog_q;
SemaphoreHandle_t randMutex;

struct debugEvent {
    const char *format;
    uint32_t data[3];
    TickType_t timestamp;
};

// Debug function to send formatted messages to the queue
void debug(const char *format, uint32_t d1, uint32_t d2, uint32_t d3) {
    struct debugEvent e;
    e.format = format;
    e.data[0] = d1;
    e.data[1] = d2;
    e.data[2] = d3;
    e.timestamp = xTaskGetTickCount();

    // Send the event to the queue
    xQueueSend(syslog_q, &e, portMAX_DELAY);
}

// Debug Task: Reads from the queue and prints the debug message

void debugTask(void *pvParameters) {
    char buffer[64];
    struct debugEvent e;

    while (1) {
        // Receive the event from the queue and print with timestamp
        if (xQueueReceive(syslog_q, &e, portMAX_DELAY) == pdPASS) {
            snprintf(buffer, 64, e.format, e.data[0], e.data[1], e.data[2]);
            printf("%lu: %s", e.timestamp, buffer);
        }
    }
}

// Button Debouncing Function
bool debounceButton(uint pin) {
    if (!gpio_get(pin)) {  // Active low button press detected
        vTaskDelay(pdMS_TO_TICKS(20));  // Short delay to debounce
        if (!gpio_get(pin)) {  // Check again after debounce delay
            return true;  // Button press is valid
        }
    }
    return false;  // No valid press detected
}

// Task 1: Button Task (monitors button press and sets event group bit)
void buttonTask(void *pvParameters) {
    while (1) {
        // Check for a debounced button press
        if (debounceButton(SW1_PIN)) {
            debug("Debounced button press detected.\n", 0, 0, 0);

            // Set event bit for Tasks 2 and 3 (bit 0)
            xEventGroupSetBits(eventGroup, BUTTON_BIT);  // Set bit 0

            // Ensure the button is released before continuing
            while (!gpio_get(SW1_PIN)) {
                vTaskDelay(pdMS_TO_TICKS(10));  // Wait for button release
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // Polling delay
    }
}

// Task 2: Worker Task (waits for BUTTON_BIT and prints elapsed ticks)
void task2(void *pvParameters) {
    TickType_t lastTick = xTaskGetTickCount();
    xEventGroupWaitBits(eventGroup, BUTTON_BIT, pdFALSE, pdFALSE, portMAX_DELAY);  // Clear bit 0


    while (1) {
        // Wait for BUTTON_BIT, clear the bit after processing
        // Execute task logic
        vTaskDelay(pdMS_TO_TICKS(1000 + rand() % 1000));  // Random delay between 1-2 seconds
        TickType_t now = xTaskGetTickCount();
        debug("Task 2 running. Elapsed ticks: %u\n", now - lastTick, 0, 0);
        lastTick = now;
    }
}

// Task 3: Worker Task (waits for BUTTON_BIT and prints elapsed ticks)
void task3(void *pvParameters) {
    TickType_t lastTick = xTaskGetTickCount();
    xEventGroupWaitBits(eventGroup, BUTTON_BIT, pdTRUE, pdFALSE, portMAX_DELAY);  // Clear bit 0


    while (1) {
        // Wait for BUTTON_BIT, clear the bit after processing
        // Execute task logic
        vTaskDelay(pdMS_TO_TICKS(1000 + rand() % 1000));  // Random delay between 1-2 seconds
        TickType_t now = xTaskGetTickCount();
        debug("Task 3 running. Elapsed ticks: %u\n", now - lastTick, 0, 0);
        lastTick = now;
    }
}

// Initialize GPIO Pins
void init_pins(void) {
    // Initialize tactile switches
    gpio_init(SW1_PIN);
    gpio_set_dir(SW1_PIN, GPIO_IN);
    gpio_pull_up(SW1_PIN);  // Active Low

    // Initialize LED pin
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

    // Initialize the random number generator and mutex
    srand(time(NULL));
    randMutex = xSemaphoreCreateMutex();

    // Send initialization message via debug task
    debug("System Initialized\n", 0, 0, 0);

    // Create tasks
    xTaskCreate(buttonTask, "Button Task", 1000, NULL, BUTTON_TASK_PRIORITY, NULL);
    xTaskCreate(task2, "Task 2", 1000, NULL, TASK2_PRIORITY, NULL);
    xTaskCreate(task3, "Task 3", 1000, NULL, TASK3_PRIORITY, NULL);
    xTaskCreate(debugTask, "Debug Task", 1000, NULL, DEBUG_TASK_PRIORITY, NULL);

    // Start the scheduler
    vTaskStartScheduler();

    // This point should never be reached
    for (;;);
}