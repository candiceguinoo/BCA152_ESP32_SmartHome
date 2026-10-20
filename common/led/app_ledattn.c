#include "app_ledattn.h"
#include <stdint.h>
#include "driver/gpio.h"

void appled_init(void)
{
    // Designated initializer: every field not listed is zeroed,
    // so no uninitialized garbage reaches gpio_config()
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ATTN_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Start with the attention LED off
    gpio_set_level((gpio_num_t)ATTN_LED_PIN, 0);
}

void appled_set(bool state)
{
    gpio_set_level((gpio_num_t)ATTN_LED_PIN, (uint32_t)state);
}