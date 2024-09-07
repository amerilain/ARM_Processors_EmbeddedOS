#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

#include "hardware/timer.h"
extern "C" {
uint32_t read_runtime_ctr(void) {
    return timer_hw->timerawl;
}
}

struct led_params{
    uint pin;
    uint delay;
};

void blink_task(void *param)
{
    led_params *lpr = (led_params *) param;
    const uint led_pin = lpr->pin;
    const uint delay = lpr->delay;
    gpio_init(led_pin);
    gpio_set_dir(led_pin, GPIO_OUT);
    while (true) {
        gpio_put(led_pin, 1);
        vTaskDelay(delay);
        gpio_put(led_pin, 0);
        vTaskDelay(delay);
    }
}

int main()
{
    static led_params lp1 = { .pin = 20, .delay = 300 };
    static led_params lp2 = { .pin = 21, .delay = 450 };

    xTaskCreate(blink_task, "LED_1", 256, (void *) &lp1, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(blink_task, "LED_2", 256, (void *) &lp2, tskIDLE_PRIORITY + 1, NULL);

    vTaskStartScheduler();

    while(1){};
}
