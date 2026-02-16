/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>

#include "app_priv.h"

//#include <app/server/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>

#include <app-common/zap-generated/ids/Attributes.h> // For Attribute IDs

#include "dishwasher_manager.h"
#include "status_display.h"

#include "esp_netif_sntp.h"

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

static const char *TAG = "app_main";
static uint16_t dish_washer_endpoint_id = 0;
static uint16_t device_energy_manager_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace esp_matter::cluster;
using namespace chip::app::Clusters::DeviceEnergyManagement;

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type)
    {
    case chip::DeviceLayer::DeviceEventType::kTimeSyncChange:
        ESP_LOGI(TAG, "Time Sync Change");
        break;

    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address Changed");
        ESP_LOGI(TAG, "Synchronizing time with SNTP server");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        StatusDisplayMgr().SetCommissioningCode("", 0);
        DishwasherMgr().TurnOnPower();
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
    {
        // ESP_LOGI(TAG, "Commissioning window opened");

        // chip::RendezvousInformationFlags rendezvoudFlags = chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE);

        // chip::PayloadContents payload;
        // GetPayloadContents(payload, rendezvoudFlags);

        // char payloadBuffer[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1];
        // chip::MutableCharSpan qrCode(payloadBuffer);

        // if (GetQRCode(qrCode, payload) == CHIP_NO_ERROR)
        // {
        //     ESP_LOGI(TAG,"Generated QR CODE [%d]: %s", qrCode.size(), qrCode.data());
        //     StatusDisplayMgr().SetCommissioningCode(qrCode.data(), qrCode.size());
        // }
        // else
        // {
        //     ESP_LOGE(TAG, "Failed to generate the commissioning QR code");
        // }
    }
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    case chip::DeviceLayer::DeviceEventType::kServerReady:
        ESP_LOGI(TAG, "Server is ready!");
        break;

    default:
        break;
    }
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id, uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    ESP_LOGI(TAG, "app_attribute_update_cb");

    if (type == POST_UPDATE)
    {
        if (endpoint_id == 0x01)
        {
            // This must be the dead front, OnOff Cluster.
            //
            if (cluster_id == OnOff::Id)
            {
                if (attribute_id == OnOff::Attributes::OnOff::Id)
                {
                    ESP_LOGI(TAG, "DeadFront OnOff attribute updated to: %s!", val->val.b ? "on" : "off");

                    if (val->val.b)
                    {
                        DishwasherMgr().TurnOnPower();
                    }
                    else
                    {
                        DishwasherMgr().TurnOffPower();
                    }
                }
            }
        }
    }

    return ESP_OK;
}

static void esp_sntp_time_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP time has been set successfully!");
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    //ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    static OperationalStateDelegate operational_state_delegate;

    dish_washer::config_t dish_washer_config;
    dish_washer_config.operational_state.delegate = &operational_state_delegate; // Set to nullptr if not using a delegate

    endpoint_t *endpoint = dish_washer::create(node, &dish_washer_config, ENDPOINT_FLAG_NONE, NULL);
    //ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create dishwasher endpoint"));

    // OperationalState is a mandatory cluster for the dishwasher endpoint.
    // The countdown time attribute is optional, so we must be add it to the cluster.
    // The commands e.g. start, stop etc. are optional, so we must add them to the cluster.
    //
    esp_matter::cluster_t *operational_state_cluster = esp_matter::cluster::get(endpoint, chip::app::Clusters::OperationalState::Id);

    esp_matter::cluster::operational_state::attribute::create_countdown_time(operational_state_cluster, 0);

    esp_matter::cluster::operational_state::command::create_start(operational_state_cluster);
    esp_matter::cluster::operational_state::command::create_stop(operational_state_cluster);
    esp_matter::cluster::operational_state::command::create_pause(operational_state_cluster);
    esp_matter::cluster::operational_state::command::create_resume(operational_state_cluster);

    // Add the DishwasherMode cluster.
    //
    //static DishwasherModeDelegate dish_washer_mode_delegate;

    esp_matter::cluster::dish_washer_mode::config_t dish_washer_mode_config;

    // dish_washer_mode_config.delegate = &dish_washer_mode_delegate;
    // dish_washer_mode_config.current_mode = DishwasherMode::ModeNormal;

    esp_matter::cluster_t *dish_washer_mode_cluster = esp_matter::cluster::dish_washer_mode::create(endpoint, &dish_washer_mode_config, CLUSTER_FLAG_SERVER);
    //ABORT_APP_ON_FAILURE(dish_washer_mode_cluster != nullptr, ESP_LOGE(TAG, "Failed to create dishwashermode cluster"));

    esp_matter::cluster::mode_base::attribute::create_supported_modes(dish_washer_mode_cluster, NULL, 0, 0);

    esp_matter::cluster::mode_base::command::create_change_to_mode(dish_washer_mode_cluster);

    // Add the On/Off cluster to the dishwasher endpoint and mark it with the dead front behavior feature.
    //
    // esp_matter::cluster::on_off::config_t on_off_config;
    // on_off_config.on_off = false; // Initial state of the On/Off cluster
    // esp_matter::cluster_t *on_off_cluster = esp_matter::cluster::on_off::create(endpoint, &on_off_config, CLUSTER_FLAG_SERVER);

    // esp_matter::cluster::on_off::feature::dead_front_behavior::add(on_off_cluster);

    dish_washer_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Dishwasher created with endpoint_id %d", dish_washer_endpoint_id);

    /*
     * Add DeviceEnergyManagement
     */
    esp_matter::endpoint::device_energy_management::config_t device_energy_management_config;
    // device_energy_management_config.device_energy_management.feature_flags = esp_matter::cluster::device_energy_management::feature::power_forecast_reporting::get_id() | esp_matter::cluster::device_energy_management::feature::start_time_adjustment::get_id();
    device_energy_management_config.device_energy_management.delegate = &device_energy_management_delegate;

    endpoint_t *device_energy_management_endpoint = esp_matter::endpoint::device_energy_management::create(node, &device_energy_management_config, ENDPOINT_FLAG_NONE, NULL);
    //ABORT_APP_ON_FAILURE(device_energy_management_endpoint != nullptr, ESP_LOGE(TAG, "Failed to create device energy management endpoint"));

    device_energy_manager_endpoint_id = endpoint::get_id(device_energy_management_endpoint);
    ESP_LOGI(TAG, "Device Energy Manager created with endpoint_id %d", device_energy_manager_endpoint_id);

    err = StatusDisplayMgr().Init();
    //ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "StatusDisplay::Init() failed, err:%d", err));

    err = DishwasherMgr().Init();
    //ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "DishwasherMgr::Init() failed, err:%d", err));

    app_driver_init();

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("2.pool.ntp.org");
    config.sync_cb = esp_sntp_time_cb;
    esp_netif_sntp_init(&config);

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    //ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::init();
#endif
}
