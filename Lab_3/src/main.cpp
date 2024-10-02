#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <cstdio>
#include <cstring>
#include "PicoOsUart.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

extern "C" {
uint32_t read_runtime_ctr(void) {
    return timer_hw->timerawl;
}
}

#define LED_PIN 21
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define UART_BAUD_RATE 115200

TimerHandle_t inactivityTimer;
TimerHandle_t ledToggleTimer;

static char rxBuffer[128];
static int bufferIndex = 0;
static int ledToggleInterval = 5000; // Default to 5 seconds
static TickType_t lastToggleTime = 0; // For 'time' command

PicoOsUart myUart(0, UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);

// toggle LED and send message
void vToggleTimerCallback(TimerHandle_t) {
    bool currentState = gpio_get(LED_PIN);
    gpio_put(LED_PIN, !currentState);
    myUart.send("LED toggled\n");
    lastToggleTime = xTaskGetTickCount();
}

void vInactivityTimerCallback(TimerHandle_t) {
    myUart.send("[Inactive]\n");
    bufferIndex = 0;
    memset(rxBuffer, 0, sizeof(rxBuffer));  // Clear buffer
}

void processCommand(PicoOsUart* uart, const char* command) {
    char outputBuffer[256];
    snprintf(outputBuffer, sizeof(outputBuffer), "Processing command: %s\n", command);
    uart->send(outputBuffer);  // Use UART

    if (strcmp(command, "help") == 0) {
        const char* helpMessage =
                "Commands:\n"
                "help - display this message\n"
                "interval <number> - set the LED toggle interval\n"
                "time - show time since last LED toggle\n";
        uart->send(helpMessage);
    } else if (strncmp(command, "interval", 8) == 0) {
        float interval = atof(command + 9) * 1000;
        if (interval > 0) {
            ledToggleInterval = interval;
            if (xTimerChangePeriod(ledToggleTimer, pdMS_TO_TICKS(ledToggleInterval), portMAX_DELAY) != pdPASS) {
                uart->send("Failed to change LED timer period\n");
            } else {
                snprintf(outputBuffer, sizeof(outputBuffer), "LED toggle interval set to %.2f seconds.\n", interval / 1000);
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

// UART receive handler
void uartReceiveHandler(PicoOsUart* uart) {
    uint8_t c;

    while (uart->read(&c, 1, 10) > 0) {
        // Reset inactivity timer
        xTimerReset(inactivityTimer, portMAX_DELAY);

        if (c >= 32 && c <= 126) {  // Printable characters
            uart->write(&c, 1);  // Echo back

            if (bufferIndex < sizeof(rxBuffer) - 1) {
                rxBuffer[bufferIndex++] = c;  // Add to buffer
            } else {
                uart->send("\r\nBuffer overflow\r\n");
                bufferIndex = 0;  // Reset buffer
            }
        } else if (c == '\r' || c == '\n') {  // Newline or carriage return
            uart->send("\r\n");  // Echo newline

            rxBuffer[bufferIndex] = '\0';  // Null-terminate
            processCommand(uart, rxBuffer);  // Process command

            bufferIndex = 0;  // Reset buffer
        }
    }
}

// FreeRTOS task to handle UART
void uartTask(void* pvParameters) {
    auto* uart = static_cast<PicoOsUart*>(pvParameters);

    for (;;) {
        uartReceiveHandler(uart);  // Continuously handle UART reception
        //vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int main() {
    // Initialize GPIO for LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // create two timers
    ledToggleTimer = xTimerCreate("LED Timer", pdMS_TO_TICKS(ledToggleInterval), pdTRUE, nullptr, vToggleTimerCallback);
    inactivityTimer = xTimerCreate("Inactivity Timer", pdMS_TO_TICKS(30000), pdTRUE, nullptr, vInactivityTimerCallback);

    // error check
    if (ledToggleTimer == nullptr || xTimerStart(ledToggleTimer, 0) != pdPASS ||
        inactivityTimer == nullptr || xTimerStart(inactivityTimer, 0) != pdPASS) {
        myUart.send("Failed to initialize timers\n");
    }

    // Create UART task
    if (xTaskCreate(uartTask, "UART Task", configMINIMAL_STACK_SIZE + 256, &myUart, tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
        myUart.send("Failed to create UART task\n");
    }

    vTaskStartScheduler();

    for (;;);
}