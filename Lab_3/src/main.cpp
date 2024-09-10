#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <stdio.h>
#include <string.h>
#include "PicoOsUart.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"

extern "C" {
uint32_t read_runtime_ctr(void) {
    return timer_hw->timerawl;
}
}
// Define constants
#define GREEN_LED_PIN 21

// Declare timers
TimerHandle_t inactivityTimer;
TimerHandle_t ledToggleTimer;

// Variables
static char rxBuffer[128];
static int bufferIndex = 0;
static int ledToggleInterval = 5000; // Default to 5 seconds

// Callback for LED toggle timer
void vLedToggleTimerCallback(TimerHandle_t xTimer) {
    gpio_xor_mask(1u << GREEN_LED_PIN); // Toggle LED
    printf("LED toggled\n");
}

// Function to process commands
void processCommand(const char* command) {
    printf("Processing command: %s\n", command);  // Debug print
    if (strcmp(command, "help") == 0) {
        printf("Commands:\n");
        printf("help - display this message\n");
        printf("interval <number> - set the LED toggle interval\n");
        printf("time - show time since last LED toggle\n");
    } else if (strncmp(command, "interval", 8) == 0) {
        int interval = atoi(command + 9) * 1000;
        if (interval > 0) {
            ledToggleInterval = interval;
            xTimerChangePeriod(ledToggleTimer, pdMS_TO_TICKS(ledToggleInterval), 0);
            printf("LED toggle interval set to %d seconds.\n", interval / 1000);
        }
    } else if (strcmp(command, "time") == 0) {
        printf("This feature will track time after toggling LED.\n");
    } else {
        printf("Unknown command\n");
    }
}

// UART receive handler
void uartReceiveHandler() {
    memset(rxBuffer, 0, sizeof(rxBuffer));  // Clear buffer
    char c = getchar_timeout_us(10000);
    if (c != PICO_ERROR_TIMEOUT) {
        printf("Received: %c\n", c);  // Debug: print each character
        if (c == '\n') {
            rxBuffer[bufferIndex] = '\0';
            processCommand(rxBuffer);
            bufferIndex = 0;
        } else {
            rxBuffer[bufferIndex++] = c;
        }
    }
}

// FreeRTOS task to handle UART
void uartTask(void* pvParameters) {
    for (;;) {
        uartReceiveHandler();  // Continuously handle UART reception
        vTaskDelay(pdMS_TO_TICKS(100));  // Small delay to prevent busy loop
    }
}

int main() {
    stdio_init_all();  // Initialize UART for printf

    // Initialize GPIO for LED
    gpio_init(GREEN_LED_PIN);
    gpio_set_dir(GREEN_LED_PIN, GPIO_OUT);

    // Create FreeRTOS tasks for UART handling
    xTaskCreate(uartTask, "UART Task", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);

    // Create LED toggle timer
    ledToggleTimer = xTimerCreate("LED Timer", pdMS_TO_TICKS(ledToggleInterval), pdTRUE, 0, vLedToggleTimerCallback);
    xTimerStart(ledToggleTimer, 0);

    // Start FreeRTOS scheduler
    vTaskStartScheduler();

    // Loop should never be reached in FreeRTOS
    for (;;);
}