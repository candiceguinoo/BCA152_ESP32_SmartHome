#include "app_sw.h"

#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TAG "app.sw"

static bool m_status;

void init_hw(void)
{
    m_status = false;

    // Preload the OFF level so the relay does not click on at startup
    gpio_set_level((gpio_num_t)RELAY_PIN, RELAY_OFF_LEVEL);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = (RELAY_OFF_LEVEL ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE),
        .pull_down_en = (RELAY_OFF_LEVEL ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE),
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level((gpio_num_t)RELAY_PIN, RELAY_OFF_LEVEL);
}

void switch_set(bool status)
{
    gpio_set_level((gpio_num_t)RELAY_PIN, status ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
    m_status = status;
    ESP_LOGI(TAG, "relay %s", status ? "ON" : "OFF");
}

bool switch_get(void)
{
    return m_status;
}