#include "app_ip.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#define MAX_RETRY 10

static const char *TAG = "app.ip";
static int retry_cnt = 0;
static connect_wifi_params_t m_params;

static void handle_wifi_connection(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "STA started, connecting to \"%s\"", WIFI_SSID);
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "disconnected, reason=%d, retry %d/%d",
                 d->reason, retry_cnt, MAX_RETRY);

        if (retry_cnt++ < MAX_RETRY)
        {
            esp_wifi_connect();
        }
        else
        {
            ESP_LOGE(TAG, "giving up after %d retries", MAX_RETRY);
            if (m_params.on_failed)
            {
                m_params.on_failed();
            }
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        retry_cnt = 0;
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

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default(); // returns an error if already created; safe to ignore
    esp_netif_create_default_wifi_sta();

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &handle_wifi_connection, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &handle_wifi_connection, NULL);

    ESP_LOGI(TAG, "ssid len=%d, pass len=%d",
             (int)strlen(WIFI_SSID), (int)strlen(WIFI_PASS));
    if (strlen(WIFI_SSID) == 0)
    {
        ESP_LOGE(TAG, "WIFI_SSID is empty! Fix your define / build flag.");
    }

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
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}