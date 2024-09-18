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

#define GREEN_LED_PIN 21
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define UART_BAUD_RATE 115200

TimerHandle_t inactivityTimer;
TimerHandle_t ledToggleTimer;

static char rxBuffer[128];
static int bufferIndex = 0;
static int ledToggleInterval = 5000; // Default to 5 seconds
static TickType_t lastToggleTime = 0; // For the 'time' command

PicoOsUart myUart(0, UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);

void vToggleTimerCallback(TimerHandle_t xTimer) {
    gpio_xor_mask(1u << GREEN_LED_PIN); // Toggle LED
    myUart.send("LED toggled\n");
    lastToggleTime = xTaskGetTickCount();
}

void vInactivityTimerCallback(TimerHandle_t xTimer) {
    myUart.send("[Inactive]\n");
    bufferIndex = 0;
    memset(rxBuffer, 0, sizeof(rxBuffer));  // Clear buffer
}

void processCommand(PicoOsUart* uart, const char* command) {
    char outputBuffer[256];
    printf("Processing command: %s\n", command);  // Debug print

    if (strcmp(command, "help") == 0) {
        const char* helpMessage =
                "Commands:\n"
                "help - display this message\n"
                "interval <number> - set the LED toggle interval\n"
                "time - show time since last LED toggle\n";
        uart->send(helpMessage);
    } else if (strncmp(command, "interval", 8) == 0) {
        int interval = atoi(command + 9) * 1000;
        if (interval > 0) {
            ledToggleInterval = interval;
            if (xTimerChangePeriod(ledToggleTimer, pdMS_TO_TICKS(ledToggleInterval), portMAX_DELAY) != pdPASS) {
                uart->send("Failed to change LED timer period\n");
            } else {
                snprintf(outputBuffer, sizeof(outputBuffer), "LED toggle interval set to %d seconds.\n", interval / 1000);
                uart->send(outputBuffer);
            }
        } else {
            uart->send("Invalid interval value.\n");
        }
    } else if (strcmp(command, "time") == 0) {
        TickType_t currentTime = xTaskGetTickCount();
        TickType_t timeSinceToggle = currentTime - lastToggleTime;
        float seconds = timeSinceToggle / (float)configTICK_RATE_HZ;
        snprintf(outputBuffer, sizeof(outputBuffer), "Time since last LED toggle: %.1f seconds\n", seconds);
        uart->send(outputBuffer);
    } else {
        uart->send("Unknown command\n");
    }

    // Clear the buffer after processing
    memset(rxBuffer, 0, sizeof(rxBuffer));
    bufferIndex = 0;
}

// UART receive handler using the PicoOsUart instance
void uartReceiveHandler(PicoOsUart* uart) {
    uint8_t c;
    int bytesRead;

    while ((bytesRead = uart->read(&c, 1, 0)) > 0) {
        if (xTimerReset(inactivityTimer, portMAX_DELAY) != pdPASS) {
            if (xTimerStart(inactivityTimer, portMAX_DELAY) != pdPASS) {
                uart->send("Failed to reset inactivity timer\n");
            }
        }

        if (c >= 32 && c <= 126) {
            // Echo back
            uart->write(&c, 1);

            if (bufferIndex < sizeof(rxBuffer) - 1) {
                // Add to buffer
                rxBuffer[bufferIndex++] = c;
            } else {
                // Buffer overflow
                uart->send("\r\nBuffer overflow\r\n");
                bufferIndex = 0;
                memset(rxBuffer, 0, sizeof(rxBuffer));
            }
        } else if (c == '\r' || c == '\n') {
            // Echo newline characters
            uart->send("\r\n");

            // Null-terminate and process the command
            rxBuffer[bufferIndex] = '\0';
            processCommand(uart, rxBuffer);

            // Reset buffer
            bufferIndex = 0;
            memset(rxBuffer, 0, sizeof(rxBuffer));
        } else if (c == 8 || c == 127) {  // Backspace or DEL
            if (bufferIndex > 0) {
                // Move cursor back, overwrite character, and move back again
                uart->send("\b \b");

                // Remove character from buffer
                bufferIndex--;
            }
            // If bufferIndex is zero, do nothing (no characters to delete)
        } else {
            // Ignore other control characters
        }
    }
}

// FreeRTOS task to handle UART
void uartTask(void* pvParameters) {
    PicoOsUart* uart = static_cast<PicoOsUart*>(pvParameters);

    for (;;) {
        uartReceiveHandler(uart);  // Continuously handle UART reception
        vTaskDelay(pdMS_TO_TICKS(10));  // Adjust delay as needed
    }
}

int main() {
    // Initialize GPIO for LED
    gpio_init(GREEN_LED_PIN);
    gpio_set_dir(GREEN_LED_PIN, GPIO_OUT);

    // Create LED toggle timer
    ledToggleTimer = xTimerCreate("LED Timer", pdMS_TO_TICKS(ledToggleInterval), pdTRUE, 0, vToggleTimerCallback);
    if (ledToggleTimer == NULL) {
        // Handle error: Timer creation failed
        myUart.send("Failed to create LED toggle timer\n");
    } else {
        if (xTimerStart(ledToggleTimer, 0) != pdPASS) {
            // Handle error: Timer start failed
            myUart.send("Failed to start LED toggle timer\n");
        }
    }

    // Create inactivity timer as auto-reload
    inactivityTimer = xTimerCreate("Inactivity Timer", pdMS_TO_TICKS(30000), pdTRUE, 0, vInactivityTimerCallback);
    if (inactivityTimer == NULL) {
        // Handle error: Timer creation failed
        myUart.send("Failed to create inactivity timer\n");
    } else {
        if (xTimerStart(inactivityTimer, 0) != pdPASS) {
            // Handle error: Timer start failed
            myUart.send("Failed to start inactivity timer\n");
        }
    }

    // Create FreeRTOS tasks for UART handling, passing the UART instance
    if (xTaskCreate(uartTask, "UART Task", configMINIMAL_STACK_SIZE + 256, &myUart, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        // Handle error: Task creation failed
        myUart.send("Failed to create UART task\n");
    }

    // Start FreeRTOS scheduler
    vTaskStartScheduler();

    // Loop should never be reached in FreeRTOS
    for (;;);
}