#include "app_blecommon.h"

#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "esp_ble_mesh_provisioning_api.h"

#define TAG "app.ble"

esp_err_t appble_nvs_init(void)
{
    static bool initialized;
    if (initialized)
    {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "NVS initialization failed (%s); NVS was NOT erased and startup is stopping. "
                 "Manual NVS recovery may require erasing the NVS partition; doing so removes "
                 "BLE Mesh keys/configuration and requires reprovisioning.",
                 esp_err_to_name(err));
        return err;
    }

    initialized = true;
    return ESP_OK;
}

void appble_log_mesh_restore_state(void)
{
    if (!esp_ble_mesh_node_is_provisioned())
    {
        ESP_LOGW(TAG, "BLE Mesh node is not provisioned; provision it before expecting Mesh communication");
        return;
    }

    uint16_t address = esp_ble_mesh_get_primary_element_address();
    if (address == ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        ESP_LOGW(TAG, "BLE Mesh reports provisioned but has no primary element address");
        return;
    }

    ESP_LOGI(TAG, "restored provisioned BLE Mesh node at primary address 0x%04x",
             address);
}

void appble_get_dev_uuid(uint8_t *dev_uuid)
{
    if (dev_uuid == NULL)
    {
        ESP_LOGE(TAG, "%s, Invalid device uuid", __func__);
        return;
    }

    memcpy(dev_uuid + 2, esp_bt_dev_get_address(), BD_ADDR_LEN);
}

esp_err_t appble_bt_init(void)
{
    esp_err_t ret;

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret)
    {
        ESP_LOGE(TAG, "%s initialize controller failed", __func__);
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret)
    {
        ESP_LOGE(TAG, "%s enable controller failed", __func__);
        return ret;
    }
    ret = esp_bluedroid_init();
    if (ret)
    {
        ESP_LOGE(TAG, "%s init bluetooth failed", __func__);
        return ret;
    }
    ret = esp_bluedroid_enable();
    if (ret)
    {
        ESP_LOGE(TAG, "%s enable bluetooth failed", __func__);
        return ret;
    }

    return ret;
}

esp_err_t appble_set_device_name(const char *name)
{
    if (name == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_ble_gap_set_device_name(name);
    if (err != ESP_OK)
    {
        return err;
    }

    return esp_ble_mesh_set_unprovisioned_device_name(name);
}

static void reboot_after_ble_reset(void *arg)
{
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    esp_restart();
}

void appble_provisioning_handler(esp_ble_mesh_prov_cb_event_t event,
                                 esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event)
    {
    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
        if (param->node_set_unprov_dev_name_comp.err_code == 0)
        {
            ESP_LOGI(TAG, "unprovisioned device name configured");
        }
        else
        {
            ESP_LOGE(TAG, "failed to configure unprovisioned device name: %d",
                     param->node_set_unprov_dev_name_comp.err_code);
        }
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "provisioned. addr: 0x%04x", param->node_prov_complete.addr);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
    {
        ESP_LOGW(TAG, "Mesh reset requested; local Mesh settings will be cleared and reprovisioning will be required");
        esp_err_t err = esp_ble_mesh_node_local_reset();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Mesh node reset failed: %s", esp_err_to_name(err));
            break;
        }
        if (xTaskCreate(reboot_after_ble_reset, "reboot",
                        configMINIMAL_STACK_SIZE, NULL, 1, NULL) != pdPASS)
        {
            ESP_LOGE(TAG, "failed to create reboot task after Mesh reset");
        }
        break;
    }
    default:
        break;
    }
}

void appble_config_handler(esp_ble_mesh_cfg_server_cb_event_t event,
                           esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT)
    {
        switch (param->ctx.recv_op)
        {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            ESP_LOGI(TAG, "config: app key added");
            break;
        default:
            break;
        }
    }
}

static appble_attn_cbs_t attn_cbs = {NULL, NULL};

void appble_health_evt_handler(esp_ble_mesh_health_server_cb_event_t event,
                               esp_ble_mesh_health_server_cb_param_t *param)
{
    ESP_LOGI(TAG, "health server event: %d", event);
    switch (event)
    {
    case ESP_BLE_MESH_HEALTH_SERVER_ATTENTION_ON_EVT:
        if (attn_cbs.attn_on != NULL)
        {
            attn_cbs.attn_on();
        }
        break;
    case ESP_BLE_MESH_HEALTH_SERVER_ATTENTION_OFF_EVT:
        if (attn_cbs.attn_off != NULL)
        {
            attn_cbs.attn_off();
        }
        break;
    default:
        break;
    }
}

void appble_set_attn_cbs(appble_attn_cbs_t cbs)
{
    attn_cbs = cbs;
}