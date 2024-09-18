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

const uint LED_PIN = 21;
const uint DEBOUNCE_DELAY_MS = 20;
const uint QUEUE_SIZE = 20;
const uint POLL_DELAY_MS = 10;

const uint8_t unlockSequence[] = {0, 0, 2, 1, 2};
const size_t unlockSequenceLength = sizeof(unlockSequence) / sizeof(unlockSequence[0]);

QueueHandle_t buttonQueue;

class Button {
public:
    Button(uint8_t id, uint pin) : buttonId(id), buttonPin(pin) {
        gpio_init(buttonPin);
        gpio_set_dir(buttonPin, GPIO_IN);
        gpio_pull_up(buttonPin);
    }

    [[nodiscard]] bool isPressed() const {
        return !gpio_get(buttonPin);  // Active low
    }

    [[nodiscard]] uint8_t getId() const {
        return buttonId;
    }

    [[nodiscard]] bool debounce() const {
        if (isPressed()) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_DELAY_MS));  // Debounce delay
            if (isPressed()) {
                return true;
            }
        }
        return false;
    }

private:
    uint8_t buttonId;
    uint buttonPin;
};

void button_task(void *param) {
    auto *button = (Button *)param;

    while (true) {
        // Wait for button press and debounce
        if (button->debounce()) {
            printf("Button %d pressed\n", button->getId());
            while (button->isPressed()) {
                vTaskDelay(pdMS_TO_TICKS(POLL_DELAY_MS));  //check for release
            }

            // send the button press event to the queue
            uint8_t buttonId = button->getId();
            if (xQueueSend(buttonQueue, &buttonId, portMAX_DELAY) != pdPASS) {
                printf("Failed to send button press to queue\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_DELAY_MS));
    }
}

void sequence_task(void *param) {
    uint8_t receivedButton;
    size_t sequenceIndex = 0;
    bool sequenceStarted = false;

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, false);  // Set LED off

    while (true) {
        if (xQueueReceive(buttonQueue, &receivedButton, pdMS_TO_TICKS(5000)) == pdPASS) {
            sequenceStarted = true;
            printf("Received button %d\n", receivedButton);

            if (receivedButton == unlockSequence[sequenceIndex]) {
                sequenceIndex++;
                printf("Correct button! Sequence index: %zu\n", sequenceIndex);

                if (sequenceIndex == unlockSequenceLength) {
                    // unlock and blink LED
                    printf("Sequence complete! Unlocking...\n");
                    for (int i = 0; i < 3; i++) {
                        gpio_put(LED_PIN, true);
                        vTaskDelay(pdMS_TO_TICKS(200));
                        gpio_put(LED_PIN, false);
                        vTaskDelay(pdMS_TO_TICKS(200));
                    }
                    sequenceIndex = 0;  // Reset sequence
                    sequenceStarted = false;
                }
            } else {
                printf("Wrong button! Resetting sequence.\n");
                sequenceIndex = 0;
                sequenceStarted = false;
            }
        } else if (sequenceStarted) {
            printf("Timeout! Resetting sequence.\n");
            sequenceIndex = 0;
            sequenceStarted = false;
        }
    }
}

int main() {
    stdio_init_all();

    buttonQueue = xQueueCreate(QUEUE_SIZE, sizeof(uint8_t));

    if (buttonQueue == nullptr) {
        printf("Failed to create buttonQueue\n");
        while (true);
    }

    static Button button0(0, 9);  // SW0
    static Button button1(1, 8);  // SW1
    static Button button2(2, 7);  // SW2

    xTaskCreate(button_task, "Button_SW0", 256, (void *)&button0, tskIDLE_PRIORITY + 3, nullptr);
    xTaskCreate(button_task, "Button_SW1", 256, (void *)&button1, tskIDLE_PRIORITY + 3, nullptr);
    xTaskCreate(button_task, "Button_SW2", 256, (void *)&button2, tskIDLE_PRIORITY + 3, nullptr);

    xTaskCreate(sequence_task, "Sequence", 256, nullptr, tskIDLE_PRIORITY + 2, nullptr);

    vTaskStartScheduler();

    while (true) {}
}