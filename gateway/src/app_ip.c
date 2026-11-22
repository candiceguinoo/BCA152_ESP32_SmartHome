#include "app_ip.h"

#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "app.ip";

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

static uint32_t retry_cnt;
static connect_wifi_params_t m_params;
static TaskHandle_t retry_task_handle;
static volatile bool wifi_connected;

static void wifi_retry_task(void *arg)
{
    while (true)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        retry_cnt = 0;
        while (!wifi_connected)
        {
            uint32_t exponent = retry_cnt < 6U ? retry_cnt++ : 6U;
            uint32_t delay_seconds = 1U << exponent;
            if (retry_cnt > 6U)
            {
                retry_cnt = 6U;
            }
            vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000U));

            if (!wifi_connected)
            {
                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK)
                {
                    ESP_LOGW(TAG, "Wi-Fi reconnect request failed: %s",
                             esp_err_to_name(err));
                }
            }
        }
    }
}

static void handle_wifi_connection(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "STA started; connecting");
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
            xTaskNotifyGive(retry_task_handle);
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)event_data;
        wifi_connected = false;
        ESP_LOGW(TAG, "disconnected, reason=%d; reconnecting with capped backoff",
                 d->reason);
        xTaskNotifyGive(retry_task_handle);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        wifi_connected = true;
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "open this in your browser: http://" IPSTR,
                 IP2STR(&event->ip_info.ip));
        if (m_params.on_connected)
        {
            m_params.on_connected();
        }
    }
}

void appip_connect_wifi(connect_wifi_params_t p)
{
    m_params = p;

    if (strlen(WIFI_SSID) == 0)
    {
        ESP_LOGE(TAG, "WIFI_SSID is empty; provide it through the build environment.");
        if (m_params.on_failed)
        {
            m_params.on_failed();
        }
        return;
    }

    if (xTaskCreate(wifi_retry_task, "wifi_retry",
                    3 * configMINIMAL_STACK_SIZE, NULL, 5,
                    &retry_task_handle) != pdPASS)
    {
        ESP_LOGE(TAG, "failed to create Wi-Fi retry task");
        if (m_params.on_failed)
        {
            m_params.on_failed();
        }
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (esp_netif_create_default_wifi_sta() == NULL)
    {
        ESP_LOGE(TAG, "failed to create default Wi-Fi station interface");
        if (m_params.on_failed)
        {
            m_params.on_failed();
        }
        return;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &handle_wifi_connection, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &handle_wifi_connection, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false},
        },
    };

    // Open network (empty password): WPA2 threshold would make connecting impossible
    if (strlen(WIFI_PASS) == 0)
    {
        ESP_LOGW(TAG, "empty password, treating network as OPEN");
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}