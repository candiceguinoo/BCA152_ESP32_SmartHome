#include "app_sensor.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TAG "app.sensor"

static bool light_level_low;
static light_changed_f light_changed_cb;

static void read_light(void *arg)
{
    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        int level = gpio_get_level((gpio_num_t)LDR_DO_PIN);
        bool dark = (level == LDR_DARK_LEVEL);
        ESP_LOGI(TAG, "LDR DO=%d -> %s", level, dark ? "DARK" : "BRIGHT");

        // Only notify the BLE network when the state actually changes
        if (dark != light_level_low)
        {
            light_level_low = dark;
            if (light_changed_cb != NULL)
            {
                light_changed_cb(light_level_low);
            }
        }
    }
}

void init_hw(light_changed_f f)
{
    light_changed_cb = f;
    light_level_low = false;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LDR_DO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    xTaskCreate(read_light, "light", 3 * configMINIMAL_STACK_SIZE, NULL, 5, NULL);
}

bool is_light_low(void)
{
    return light_level_low;
}