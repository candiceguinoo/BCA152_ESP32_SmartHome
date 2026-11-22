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
#include "esp_ble_mesh_health_model_api.h"

#define TAG "app.ble"

#define CID_ESP 0x02E5

static uint8_t dev_uuid[16] = {'L', 'S'};

static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .default_ttl = 7,
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_pub_0, 2 + 3, ROLE_NODE);

static esp_ble_mesh_gen_onoff_srv_t onoff_server_0 = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
    .rsp_ctrl.status_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
};

static uint8_t test_ids[] = {0, 1, 2};

ESP_BLE_MESH_MODEL_PUB_DEFINE(health_pub_0, 10, ROLE_NODE);

static esp_ble_mesh_health_srv_t health_server = {
    .health_test = {
        .id_count = ARRAY_SIZE(test_ids),
        .test_ids = test_ids,
        .company_id = CID_ESP,
    },
};

static TaskHandle_t publish_task_handle;
static portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;
static bool desired_state;
static bool state_pending;
static unsigned int publish_success_count;
static int last_readiness_state = -1;

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoff_pub_0, &onoff_server_0),
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

static void generic_server_cb(
    esp_ble_mesh_generic_server_cb_event_t event,
    esp_ble_mesh_generic_server_cb_param_t *param)
{
    esp_ble_mesh_model_t *model = &root_models[1];
    esp_ble_mesh_gen_onoff_srv_t *srv = &onoff_server_0;
    esp_ble_mesh_msg_ctx_t *ctx = &param->ctx;

    switch (event)
    {
    case ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT:
    case ESP_BLE_MESH_GENERIC_SERVER_RECV_SET_MSG_EVT:
        if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
            param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK)
        {
            if (ctx->recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET)
            {
                esp_ble_mesh_server_model_send_msg(
                    model,
                    ctx,
                    ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS,
                    sizeof(srv->state.onoff),
                    &srv->state.onoff);
            }
        }
        break;

    default:
        ESP_LOGW(TAG, "unexpected event (%d)", event);
        break;
    }
}

static bool model_has_app_key(const esp_ble_mesh_model_t *model,
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

static bool publish_sensor_state(bool onoff)
{
    esp_ble_mesh_model_t *model = &root_models[1];
    esp_ble_mesh_gen_onoff_srv_t *srv = &onoff_server_0;

    if (!esp_ble_mesh_node_is_provisioned())
    {
        if (last_readiness_state != 0)
        {
            ESP_LOGI(TAG, "Waiting for BLE Mesh provisioning before publishing sensor state");
            last_readiness_state = 0;
        }
        return false;
    }

    if (model->pub == NULL || model->pub->msg == NULL ||
        model->pub->publish_addr == ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        if (last_readiness_state != 1)
        {
            ESP_LOGW(TAG, "Sensor publication is not configured; configure its publication address in the provisioner");
            last_readiness_state = 1;
        }
        return false;
    }

    uint16_t app_idx = model->pub->app_idx;
    if (esp_ble_mesh_node_get_local_app_key(app_idx) == NULL ||
        !model_has_app_key(model, app_idx))
    {
        if (last_readiness_state != 2)
        {
            ESP_LOGW(TAG, "Sensor publication requires an AppKey and model binding configured by the provisioner");
            last_readiness_state = 2;
        }
        return false;
    }

    last_readiness_state = -1;

    srv->state.onoff = onoff;
    ESP_LOGI(TAG, "Publishing sensor state %s to 0x%04x",
             onoff ? "ON (DARK)" : "OFF (BRIGHT)",
             model->pub->publish_addr);

    esp_err_t err = esp_ble_mesh_model_publish(
        model,
        ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS,
        sizeof(srv->state.onoff),
        &srv->state.onoff,
        ROLE_NODE);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "BLE Mesh publication accepted");
        return true;
    }

    ESP_LOGE(TAG, "BLE Mesh publication failed: %s",
             esp_err_to_name(err));
    return false;
}

static void sensor_publish_task(void *arg)
{
    while (true)
    {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));

        portENTER_CRITICAL(&state_mux);
        bool pending = state_pending;
        bool state = desired_state;
        portEXIT_CRITICAL(&state_mux);

        if (!pending || !publish_sensor_state(state))
        {
            continue;
        }

        portENTER_CRITICAL(&state_mux);
        if (desired_state == state)
        {
            publish_success_count++;
            if (publish_success_count >= 3)
            {
                state_pending = false;
            }
        }
        portEXIT_CRITICAL(&state_mux);
    }
}

void appble_update_state(bool onoff)
{
    portENTER_CRITICAL(&state_mux);
    desired_state = onoff;
    state_pending = true;
    publish_success_count = 0;
    portEXIT_CRITICAL(&state_mux);

    if (publish_task_handle != NULL)
    {
        xTaskNotifyGive(publish_task_handle);
    }
}

void init_ble(appble_attn_cbs_t cbs)
{
    appble_set_attn_cbs(cbs);

    if (appble_bt_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "bluetooth_init failed");
        return;
    }

    appble_get_dev_uuid(dev_uuid);

    esp_ble_mesh_register_prov_callback(appble_provisioning_handler);
    esp_ble_mesh_register_config_server_callback(appble_config_handler);
    esp_ble_mesh_register_generic_server_callback(generic_server_cb);
    esp_ble_mesh_register_health_server_callback(appble_health_evt_handler);

    if (esp_ble_mesh_init(&provision, &composition) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize mesh stack");
        return;
    }

    appble_log_mesh_restore_state();

    if (xTaskCreate(sensor_publish_task, "sensor_pub",
                    3 * configMINIMAL_STACK_SIZE, NULL, 5,
                    &publish_task_handle) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create sensor publication retry task");
        return;
    }

    portENTER_CRITICAL(&state_mux);
    bool pending = state_pending;
    portEXIT_CRITICAL(&state_mux);
    if (pending)
    {
        xTaskNotifyGive(publish_task_handle);
    }

    esp_err_t err = appble_set_device_name("Light Sensor");
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set BLE device name: %s",
                 esp_err_to_name(err));
        return;
    }

    if (esp_ble_mesh_node_prov_enable(
            ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enable mesh node");
        return;
    }
}
