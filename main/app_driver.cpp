/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include "app_priv.h"
#include <app-common/zap-generated/attribute-type.h>
#include <app-common/zap-generated/cluster-enums.h>
#include <app/util/generic-callbacks.h>
#include <protocols/interaction_model/StatusCode.h>
#include "dishwasher_manager.h"
#include <esp_debug_helpers.h>
#include "iot_button.h"

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::OperationalState;
using namespace chip::app::Clusters::DeviceEnergyManagement;
using namespace chip::app::Clusters::DeviceEnergyManagement::Attributes;
using namespace chip::Protocols::InteractionModel;

static const char *TAG = "app_driver";

DataModel::Nullable<uint32_t> OperationalStateDelegate::GetCountdownTime()
{
    ESP_LOGI(TAG, "GetCountdownTime");
    uint32_t timeRemaining = DishwasherMgr().GetTimeRemaining();
    return DataModel::MakeNullable(timeRemaining);
}

CHIP_ERROR OperationalStateDelegate::GetOperationalStateAtIndex(size_t index, GenericOperationalState &operationalState)
{
    ESP_LOGI(TAG, "GetOperationalStateAtIndex");

    if (index > mOperationalStateList.size() - 1)
    {
        return CHIP_ERROR_NOT_FOUND;
    }

    operationalState = mOperationalStateList[index];

    return CHIP_NO_ERROR;
}

CHIP_ERROR OperationalStateDelegate::GetOperationalPhaseAtIndex(size_t index, MutableCharSpan &operationalPhase)
{
    ESP_LOGI(TAG, "GetOperationalPhaseAtIndex");

    if (index >= mOperationalPhaseList.size())
    {
        return CHIP_ERROR_NOT_FOUND;
    }
    return CopyCharSpanToMutableCharSpan(mOperationalPhaseList[index], operationalPhase);
}

void OperationalStateDelegate::HandlePauseStateCallback(GenericOperationalError &err)
{
    ESP_LOGI(TAG, "HandlePauseStateCallback");
    DishwasherMgr().PauseProgram();
    err.Set(to_underlying(ErrorStateEnum::kNoError));
}

void OperationalStateDelegate::HandleResumeStateCallback(GenericOperationalError &err)
{
    ESP_LOGI(TAG, "HandleResumeStateCallback");
    DishwasherMgr().ResumeProgram();
    err.Set(to_underlying(ErrorStateEnum::kNoError));
    // err.Set(to_underlying(ErrorStateEnum::kUnableToCompleteOperation));
}

void OperationalStateDelegate::HandleStartStateCallback(GenericOperationalError &err)
{
    ESP_LOGI(TAG, "HandleStartStateCallback");

    DishwasherMgr().StartProgram();
    err.Set(to_underlying(ErrorStateEnum::kNoError));
}

void OperationalStateDelegate::HandleStopStateCallback(GenericOperationalError &err)
{
    ESP_LOGI(TAG, "HandleStopStateCallback");

    DishwasherMgr().StopProgram();
    err.Set(to_underlying(ErrorStateEnum::kNoError));
}

void OperationalStateDelegate::PostAttributeChangeCallback(AttributeId attributeId, uint8_t type, uint16_t size, uint8_t *value)
{
    ESP_LOGI(TAG, "OperationalStateDelegate::PostAttributeChangeCallback");
}

static OperationalState::Instance *gOperationalStateInstance = nullptr;
static OperationalStateDelegate *gOperationalStateDelegate = nullptr;

void OperationalState::Shutdown()
{
    if (gOperationalStateInstance != nullptr)
    {
        delete gOperationalStateInstance;
        gOperationalStateInstance = nullptr;
    }
    if (gOperationalStateDelegate != nullptr)
    {
        delete gOperationalStateDelegate;
        gOperationalStateDelegate = nullptr;
    }
}

OperationalState::Instance *OperationalState::GetInstance()
{
    return gOperationalStateInstance;
}

OperationalState::OperationalStateDelegate *OperationalState::GetDelegate()
{
    return gOperationalStateDelegate;
}

void emberAfOperationalStateClusterInitCallback(chip::EndpointId endpointId)
{
    ESP_LOGI(TAG, "emberAfOperationalStateClusterInitCallback()");

    VerifyOrDie(endpointId == 1); // this cluster is only enabled for endpoint 1.
    VerifyOrDie(gOperationalStateInstance == nullptr && gOperationalStateDelegate == nullptr);

    gOperationalStateDelegate = new OperationalStateDelegate;

    gOperationalStateInstance = new OperationalState::Instance(gOperationalStateDelegate, endpointId);
    gOperationalStateInstance->SetOperationalState(to_underlying(OperationalState::OperationalStateEnum::kStopped));
    gOperationalStateInstance->SetCurrentPhase(0);
    gOperationalStateInstance->Init();

    uint8_t value = to_underlying(OperationalStateEnum::kStopped);
    gOperationalStateDelegate->PostAttributeChangeCallback(chip::app::Clusters::OperationalState::Attributes::OperationalState::Id, ZCL_INT8U_ATTRIBUTE_TYPE, sizeof(uint8_t), &value);
    gOperationalStateDelegate->PostAttributeChangeCallback(chip::app::Clusters::OperationalState::Attributes::CurrentPhase::Id, ZCL_INT8U_ATTRIBUTE_TYPE, sizeof(uint8_t), 0);
}

void emberAfOperationalStateClusterShutdownCallback(chip::EndpointId endpointId)
{
    OperationalState::Shutdown();
}

//****************************
//* DISHWASHER MODE DELEGATE *
//****************************

using chip::Protocols::InteractionModel::Status;
template <typename T>
using List = chip::app::DataModel::List<T>;
using ModeTagStructType = chip::app::Clusters::detail::Structs::ModeTagStruct::Type;

static DishwasherModeDelegate *gDishwasherModeDelegate = nullptr;
static ModeBase::Instance *gDishwasherModeInstance = nullptr;

CHIP_ERROR DishwasherModeDelegate::Init()
{
    ESP_LOGI(TAG, "DishwasherModeDelegate::Init()");
    gDishwasherModeInstance = mInstance;
    return CHIP_NO_ERROR;
}

void DishwasherModeDelegate::HandleChangeToMode(uint8_t NewMode, ModeBase::Commands::ChangeToModeResponse::Type &response)
{
    ESP_LOGI(TAG, "DishwasherModeDelegate::HandleChangeToMode()");
    DishwasherMgr().UpdateMode(NewMode);
    response.status = to_underlying(ModeBase::StatusCode::kSuccess);
}

CHIP_ERROR DishwasherModeDelegate::GetModeLabelByIndex(uint8_t modeIndex, chip::MutableCharSpan &label)
{
    ESP_LOGI(TAG, "DishwasherModeDelegate::GetModeLabelByIndex()");
    if (modeIndex >= MATTER_ARRAY_SIZE(kModeOptions))
    {
        ESP_LOGI(TAG, "CHIP_ERROR_PROVIDER_LIST_EXHAUSTED");
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    return chip::CopyCharSpanToMutableCharSpan(kModeOptions[modeIndex].label, label);
}

CHIP_ERROR DishwasherModeDelegate::GetModeValueByIndex(uint8_t modeIndex, uint8_t &value)
{
    ESP_LOGI(TAG, "DishwasherModeDelegate::GetModeValueByIndex(%d)", modeIndex);

    if (modeIndex >= MATTER_ARRAY_SIZE(kModeOptions))
    {
        ESP_LOGI(TAG, "CHIP_ERROR_PROVIDER_LIST_EXHAUSTED");
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    value = kModeOptions[modeIndex].mode;

    ESP_LOGI(TAG, "DishwasherModeDelegate::GetModeValueByIndex - Returning value %d for modeIndex: %d", value, modeIndex);

    return CHIP_NO_ERROR;
}

CHIP_ERROR DishwasherModeDelegate::GetModeTagsByIndex(uint8_t modeIndex, List<ModeTagStructType> &tags)
{
    ESP_LOGI(TAG, "DishwasherModeDelegate::GetModeTagsByIndex()");
    if (modeIndex >= MATTER_ARRAY_SIZE(kModeOptions))
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }

    if (tags.size() < kModeOptions[modeIndex].modeTags.size())
    {
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    std::copy(kModeOptions[modeIndex].modeTags.begin(), kModeOptions[modeIndex].modeTags.end(), tags.begin());
    tags.reduce_size(kModeOptions[modeIndex].modeTags.size());

    return CHIP_NO_ERROR;
}

Status DishwasherModeDelegate::SetDishwasherMode(uint8_t modeValue)
{
    ESP_LOGI(TAG, "DishwasherModeDelegate::SetDishwasherMode");

    // We can only update the DishwasherMode when it's not running.
    //
    VerifyOrReturnError(DishwasherMode::GetInstance() != nullptr, Status::InvalidInState);

    if (!DishwasherMode::GetInstance()->IsSupportedMode(modeValue))
    {
        ChipLogError(AppServer, "SetDishwasherMode bad mode");
        return Status::ConstraintError;
    }

    Status status = DishwasherMode::GetInstance()->UpdateCurrentMode(modeValue);
    if (status != Status::Success)
    {
        ChipLogError(AppServer, "SetDishwasherMode updateMode failed 0x%02x", to_underlying(status));
        return status;
    }

    return chip::Protocols::InteractionModel::Status::Success;
}

void DishwasherModeDelegate::PostAttributeChangeCallback(AttributeId attributeId, uint8_t type, uint16_t size, uint8_t *value)
{
    ESP_LOGI(TAG, "DishwasherModeDelegate::PostAttributeChangeCallback");
}

ModeBase::Instance *DishwasherMode::GetInstance()
{
    return gDishwasherModeInstance;
}

ModeBase::Delegate *DishwasherMode::GetDelegate()
{
    return gDishwasherModeDelegate;
}

void DishwasherMode::Shutdown()
{
    if (gDishwasherModeInstance != nullptr)
    {
        delete gDishwasherModeInstance;
        gDishwasherModeInstance = nullptr;
    }
    if (gDishwasherModeDelegate != nullptr)
    {
        delete gDishwasherModeDelegate;
        gDishwasherModeDelegate = nullptr;
    }
}

void emberAfDishwasherModeClusterInitCallback(chip::EndpointId endpointId)
{
    ESP_LOGI(TAG, "emberAfDishwasherModeClusterInitCallback()");

    VerifyOrDie(endpointId == 1); // this cluster is only enabled for endpoint 1.
    VerifyOrDie(gDishwasherModeDelegate == nullptr && gDishwasherModeInstance == nullptr);

    gDishwasherModeDelegate = new DishwasherMode::DishwasherModeDelegate;

    // TODO Restore the deadfront support by setting the OnOff feature.
    // gDishwasherModeInstance = new ModeBase::Instance(gDishwasherModeDelegate, 0x1, DishwasherMode::Id, chip::to_underlying(chip::app::Clusters::DishwasherMode:: ::Feature::kOnOff));

    gDishwasherModeInstance = new ModeBase::Instance(gDishwasherModeDelegate, endpointId, DishwasherMode::Id, 0);
    gDishwasherModeInstance->Init();

    uint8_t currentMode = gDishwasherModeInstance->GetCurrentMode();
    ESP_LOGI(TAG, "CurrentMode: %d", currentMode);
}

//*************************************
//* DEVICE ENERGY MANAGEMENT DELEGATE *
//*************************************

chip::app::Clusters::DeviceEnergyManagement::DeviceEnergyManagementDelegate device_energy_management_delegate;

Status DeviceEnergyManagementDelegate::PowerAdjustRequest(const int64_t powerMw, const uint32_t durationS, AdjustmentCauseEnum cause)
{
    return Status::Failure;
}

Status DeviceEnergyManagementDelegate::CancelPowerAdjustRequest()
{
    return Status::Failure;
}

Status DeviceEnergyManagementDelegate::StartTimeAdjustRequest(const uint32_t requestedStartTime, AdjustmentCauseEnum cause)
{
    ESP_LOGI(TAG, "StartTime Adjustment received: New start time: %lu", requestedStartTime);

    DishwasherMgr().AdjustStartTime(requestedStartTime);

    return Status::Success;
}

Status DeviceEnergyManagementDelegate::PauseRequest(const uint32_t duration, AdjustmentCauseEnum cause)
{
    return Status::Failure;
}

Status DeviceEnergyManagementDelegate::ResumeRequest()
{
    return Status::Failure;
}

Status DeviceEnergyManagementDelegate::ModifyForecastRequest(const uint32_t forecastID, const DataModel::DecodableList<DeviceEnergyManagement::Structs::SlotAdjustmentStruct::Type> &slotAdjustments, AdjustmentCauseEnum cause)
{
    return Status::Failure;
}

Status DeviceEnergyManagementDelegate::RequestConstraintBasedForecast(const DataModel::DecodableList<DeviceEnergyManagement::Structs::ConstraintsStruct::Type> &constraints, AdjustmentCauseEnum cause)
{
    return Status::Failure;
}

Status DeviceEnergyManagementDelegate::CancelRequest()
{
    return Status::Failure;
}

ESATypeEnum DeviceEnergyManagementDelegate::GetESAType()
{
    return ESATypeEnum::kDishwasher;
}

bool DeviceEnergyManagementDelegate::GetESACanGenerate()
{
    return false;
}

ESAStateEnum DeviceEnergyManagementDelegate::GetESAState()
{
    return ESAStateEnum::kOnline;
}

int64_t DeviceEnergyManagementDelegate::GetAbsMinPower()
{
    return 0;
}

int64_t DeviceEnergyManagementDelegate::GetAbsMaxPower()
{
    return 1000;
}

OptOutStateEnum DeviceEnergyManagementDelegate::GetOptOutState()
{
    return mOptOutState;
}

void DeviceEnergyManagementDelegate::SetOptOutState(OptOutStateEnum state)
{
    mOptOutState = state;
    MatterReportingAttributeChangeCallback(DeviceEnergyManagementDelegate::mEndpointId, DeviceEnergyManagement::Id, DeviceEnergyManagement::Attributes::OptOutState::Id);
}

CHIP_ERROR DeviceEnergyManagementDelegate::SetESAState(ESAStateEnum newValue)
{
    return CHIP_NO_ERROR;
}

chip::app::DataModel::Nullable<DeviceEnergyManagement::Structs::PowerAdjustCapabilityStruct::Type> &DeviceEnergyManagementDelegate::GetPowerAdjustmentCapability()
{
    return mPowerAdjustCapabilityStruct;
}

chip::app::DataModel::Nullable<DeviceEnergyManagement::Structs::ForecastStruct::Type> &DeviceEnergyManagementDelegate::GetForecast()
{
    ESP_LOGI(TAG, "Returning Forecast...");

    if (mForecast.IsNull())
    {
        ESP_LOGI(TAG, "Forecast is null :(");
    }
    else
    {
        ESP_LOGI(TAG, "Forecast start time: %lu", mForecast.Value().startTime);
        ESP_LOGI(TAG, "Forecast slots: %d", mForecast.Value().slots.size());
    }

    ESP_LOGI(TAG, "Current Free Memory\t%d\t\t%d", heap_caps_get_free_size(MALLOC_CAP_8BIT) - heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Largest Free Block\t%d\t\t%d", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Min. Ever Free Size\t%d\t\t%d", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL), heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));

    return mForecast;
}

CHIP_ERROR DeviceEnergyManagementDelegate::SetForecast(const chip::app::DataModel::Nullable<DeviceEnergyManagement::Structs::ForecastStruct::Type> &forecast)
{
    ESP_LOGI(TAG, "Updating Forecast on Endpoint %d...", DeviceEnergyManagementDelegate::mEndpointId);

    if (forecast.IsNull())
    {
        ESP_LOGI(TAG, "Forecast is null :(");
    }
    else
    {
        ESP_LOGI(TAG, "Forecast start time: %lu", forecast.Value().startTime);
        ESP_LOGI(TAG, "Forecast slots: %d", forecast.Value().slots.size());
    }

    mForecast = forecast;

    MatterReportingAttributeChangeCallback(DeviceEnergyManagementDelegate::mEndpointId, DeviceEnergyManagement::Id, DeviceEnergyManagement::Attributes::Forecast::Id);

    return CHIP_NO_ERROR;
}

void emberAfDeviceEnergyManagementClusterInitCallback(chip::EndpointId endpointId)
{
    ESP_LOGI(TAG, "emberAfDeviceEnergyManagerClusterInitCallback()");
}

//************************
//* DEAD FRONT BEHAVIOUR *
//************************

void emberAfOnOffClusterInitCallback(EndpointId endpoint)
{
    ESP_LOGI(TAG, "emberAfOnOffClusterInitCallback()");
}

//***********
//* BUTTONS *
//***********

static void onoff_button_single_click_cb(void *args, void *user_data)
{
    ESP_LOGI(TAG, "OnOff Clicked");
    DishwasherMgr().HandleOnOffClicked();
}

static void onoff_button_long_press_start_cb(void *args, void *user_data)
{
    ESP_LOGI(TAG, "OnOff Long Press Start");
    chip::Server::GetInstance().ScheduleFactoryReset();
}

static void start_button_single_click_cb(void *args, void *user_data)
{
    ESP_LOGI(TAG, "Start Clicked");
    DishwasherMgr().HandleStartClicked();
}

static void rotary_button_single_click_cb(void *args, void *user_data)
{
    ESP_LOGI(TAG, "Rotary Clicked");
    DishwasherMgr().HandleWheelClicked();
}

esp_err_t app_driver_init()
{
    esp_err_t err = ESP_OK;

    button_config_t onoff_config;
    memset(&onoff_config, 0, sizeof(button_config_t));

    onoff_config.type = BUTTON_TYPE_GPIO;
    onoff_config.gpio_button_config.gpio_num = GPIO_NUM_1;
    onoff_config.gpio_button_config.active_level = 0;

    button_handle_t onoff_handle = iot_button_create(&onoff_config);

    ESP_ERROR_CHECK(iot_button_register_cb(onoff_handle, BUTTON_LONG_PRESS_START, onoff_button_long_press_start_cb, NULL));
    ESP_ERROR_CHECK(iot_button_register_cb(onoff_handle, BUTTON_SINGLE_CLICK, onoff_button_single_click_cb, NULL));

    button_config_t start_config;
    memset(&start_config, 0, sizeof(button_config_t));

    start_config.type = BUTTON_TYPE_GPIO;
    start_config.gpio_button_config.gpio_num = GPIO_NUM_2;
    start_config.gpio_button_config.active_level = 0;

    button_handle_t start_handle = iot_button_create(&start_config);

    ESP_ERROR_CHECK(iot_button_register_cb(start_handle, BUTTON_SINGLE_CLICK, start_button_single_click_cb, NULL));

    button_config_t rotary_config;
    memset(&rotary_config, 0, sizeof(button_config_t));

    rotary_config.type = BUTTON_TYPE_GPIO;
    rotary_config.gpio_button_config.gpio_num = GPIO_NUM_5;
    rotary_config.gpio_button_config.active_level = 0;

    button_handle_t rotary_handle = iot_button_create(&rotary_config);

    ESP_ERROR_CHECK(iot_button_register_cb(rotary_handle, BUTTON_SINGLE_CLICK, rotary_button_single_click_cb, NULL));

    return err;
}