#include "app_ble.h"

#include "esp_log.h"

#include "esp_gap_ble_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "app.ble"

#define CID_ESP 0x02E5

static uint8_t dev_uuid[16] = {'G', 'W'};

static esp_ble_mesh_client_t onoff_client;
static uint8_t onoff_tid;
static TaskHandle_t command_task_handle;
static portMUX_TYPE command_mux = portMUX_INITIALIZER_UNLOCKED;
static bool desired_switch_state;
static bool command_pending;
static uint32_t command_generation;
static int last_command_readiness_state = -1;

static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .default_ttl = 7,
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_cli_pub, 2 + 1, ROLE_NODE);

static uint8_t test_ids[] = {0, 1, 2};
ESP_BLE_MESH_MODEL_PUB_DEFINE(health_pub_0, 10, ROLE_NODE);
static esp_ble_mesh_health_srv_t health_server = {
    .health_test = {
        .id_count = ARRAY_SIZE(test_ids),
        .test_ids = test_ids,
        .company_id = CID_ESP,
    },
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoff_cli_pub, &onoff_client),
    ESP_BLE_MESH_MODEL_HEALTH_SRV(&health_server, &health_pub_0),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .elements = elements,
    .element_count = ARRAY_SIZE(elements),
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
    .output_size = 0,
    .output_actions = 0,
};

static bool client_model_has_app_key(const esp_ble_mesh_model_t *model,
                                     uint16_t app_idx)
{
    for (size_t i = 0; i < CONFIG_BLE_MESH_MODEL_KEY_COUNT; i++)
    {
        if (model->keys[i] == app_idx)
        {
            return true;
        }
    }
    return false;
}

static bool switch_command_ready(void)
{
    int readiness_state;
    const char *message;

    if (!esp_ble_mesh_node_is_provisioned())
    {
        readiness_state = 0;
        message = "Waiting for gateway provisioning before sending switch commands";
    }
    else if (onoff_client.model == NULL)
    {
        readiness_state = 1;
        message = "Switch command model is not initialized";
    }
    else if (esp_ble_mesh_node_get_local_app_key(0) == NULL ||
             !client_model_has_app_key(onoff_client.model, 0))
    {
        readiness_state = 2;
        message = "Switch commands require AppKey 0 and model binding configured by the provisioner";
    }
    else
    {
        readiness_state = 3;
        message = "Gateway Mesh command path is ready";
    }

    if (readiness_state != last_command_readiness_state)
    {
        if (readiness_state == 3)
        {
            ESP_LOGI(TAG, "%s", message);
        }
        else
        {
            ESP_LOGW(TAG, "%s", message);
        }
        last_command_readiness_state = readiness_state;
    }

    return readiness_state == 3;
}

static esp_err_t send_switch_state(bool onoff)
{
    esp_ble_mesh_generic_client_set_state_t set = {0};
    esp_ble_mesh_client_common_param_t common = {0};

    common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK;
    common.model = onoff_client.model;
    common.ctx.net_idx = ESP_BLE_MESH_KEY_PRIMARY;
    common.ctx.app_idx = 0;
    common.ctx.addr = 0xFFFF;
    common.ctx.send_ttl = 3;
    common.msg_timeout = 0;

    set.onoff_set.op_en = false;
    set.onoff_set.onoff = onoff;
    set.onoff_set.tid = onoff_tid++;

    esp_err_t err = esp_ble_mesh_generic_client_set_state(&common, &set);
    if (err != ESP_OK)
    {
        return err;
    }
    ESP_LOGI(TAG, "Switch command accepted (state=%s, tid=%u)",
             onoff ? "ON" : "OFF", set.onoff_set.tid);
    return ESP_OK;
}

static void switch_command_task(void *arg)
{
    while (true)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        unsigned int sent_count = 0;
        uint32_t generation;

        portENTER_CRITICAL(&command_mux);
        generation = command_generation;
        portEXIT_CRITICAL(&command_mux);

        while (sent_count < 3)
        {
            portENTER_CRITICAL(&command_mux);
            bool pending = command_pending;
            bool state = desired_switch_state;
            portEXIT_CRITICAL(&command_mux);

            if (!pending)
            {
                break;
            }

            if (switch_command_ready())
            {
                esp_err_t err = send_switch_state(state);
                if (err == ESP_OK)
                {
                    sent_count++;
                }
                else
                {
                    ESP_LOGW(TAG, "Switch command send failed; retrying: %s",
                             esp_err_to_name(err));
                }
            }

            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) > 0)
            {
                sent_count = 0;
                portENTER_CRITICAL(&command_mux);
                generation = command_generation;
                portEXIT_CRITICAL(&command_mux);
            }
        }

        portENTER_CRITICAL(&command_mux);
        bool newer_command = command_generation != generation;
        if (!newer_command)
        {
            command_pending = false;
        }
        portEXIT_CRITICAL(&command_mux);
        if (newer_command)
        {
            xTaskNotifyGive(command_task_handle);
        }
    }
}

void appble_set_switch(bool onoff)
{
    portENTER_CRITICAL(&command_mux);
    desired_switch_state = onoff;
    command_pending = true;
    command_generation++;
    portEXIT_CRITICAL(&command_mux);

    if (command_task_handle != NULL)
    {
        xTaskNotifyGive(command_task_handle);
    }
}

static void generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                              esp_ble_mesh_generic_client_cb_param_t *param)
{
    switch (event)
    {
    case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT");
        break;
    default:
        break;
    }
}

void init_ble(appble_attn_cbs_t cbs)
{
    if (appble_nvs_init() != ESP_OK)
    {
        return;
    }

    appble_set_attn_cbs(cbs);

    if (appble_bt_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "bluetooth_init failed");
        return;
    }

    appble_get_dev_uuid(dev_uuid);

    esp_ble_mesh_register_prov_callback(appble_provisioning_handler);
    esp_ble_mesh_register_config_server_callback(appble_config_handler);
    esp_ble_mesh_register_health_server_callback(appble_health_evt_handler);

    esp_ble_mesh_register_generic_client_callback(generic_client_cb);

    if (esp_ble_mesh_init(&provision, &composition) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize mesh stack");
        return;
    }

    appble_log_mesh_restore_state();

    if (xTaskCreate(switch_command_task, "switch_cmd",
                    3 * configMINIMAL_STACK_SIZE, NULL, 5,
                    &command_task_handle) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create switch command retry task");
        return;
    }
    portENTER_CRITICAL(&command_mux);
    bool pending = command_pending;
    portEXIT_CRITICAL(&command_mux);
    if (pending)
    {
        xTaskNotifyGive(command_task_handle);
    }

    esp_err_t err = appble_set_device_name("Gateway");
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set BLE device name: %s",
                 esp_err_to_name(err));
        return;
    }

    if (esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enable mesh node");
        return;
    }
}