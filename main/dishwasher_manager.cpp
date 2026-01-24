#include "dishwasher_manager.h"

#include "esp_log.h"

#include <app/clusters/operational-state-server/operational-state-server.h>
#include <app/clusters/mode-base-server/mode-base-server.h>

#include "status_display.h"
#include "mode_selector.h"
#include "app_priv.h"

#include <inttypes.h>

static const char *TAG = "dishwasher_manager";

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::OperationalState;

DishwasherManager DishwasherManager::sDishwasher;

static void ProgramTick(void *arg)
{
    while (1)
    {
        DishwasherMgr().ProgressProgram();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

esp_err_t DishwasherManager::Init()
{
    ESP_LOGI(TAG, "Initializing DishwasherManager");
    ModeSelectorMgr().Init();

    xTaskCreate(ProgramTick, "ProgramTick", 4096, NULL, tskIDLE_PRIORITY, NULL);

    return ESP_OK;
}

void DishwasherManager::PresentReset()
{
    mIsShowingReset = true;
    StatusDisplayMgr().ShowResetOptions();
}

void DishwasherManager::HandleOnOffClicked()
{
    if (mIsShowingReset)
    {
        StatusDisplayMgr().HideResetOptions();
        mIsShowingReset = false;
    }
    else
    {
        TogglePower();
    }
}

void DishwasherManager::HandleStartClicked()
{
    if (!mIsPoweredOn)
    {
        ESP_LOGI(TAG, "Dishwasher is off, cannot handle start");
        return;
    }

    if (mIsShowingReset)
    {
        esp_matter::factory_reset();
        mIsShowingReset = false;
    }
    else
    {
        ToggleProgram();
    }
}

uint32_t DishwasherManager::GetTimeRemaining()
{
    return mRunningTimeRemaining;
}

void DishwasherManager::TogglePower()
{
    if (mIsPoweredOn)
    {
        TurnOffPower();
    }
    else
    {
        TurnOnPower();
    }

    // We can update the OnOff attribute directly as its managed by esp-matter.
    //
    uint16_t endpoint_id = 0x01;
    uint32_t cluster_id = OnOff::Id;
    uint32_t attribute_id = OnOff::Attributes::OnOff::Id;

    esp_matter::attribute_t *attribute = esp_matter::attribute::get(endpoint_id, cluster_id, attribute_id);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    esp_matter::attribute::get_val(attribute, &val);
    val.val.b = mIsPoweredOn;
    esp_matter::attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}

void DishwasherManager::TurnOnPower()
{
    mIsPoweredOn = true;
    StatusDisplayMgr().TurnOn();
    UpdateDishwasherDisplay();
}

void DishwasherManager::TurnOffPower()
{
    mIsPoweredOn = false;
    StopProgram();
    StatusDisplayMgr().TurnOff();
}

bool DishwasherManager::IsPoweredOn()
{
    return mIsPoweredOn;
}

void DishwasherManager::ToggleProgram()
{
    if (mState == OperationalStateEnum::kStopped)
    {
        StartProgram();
    }
    else if (mState == OperationalStateEnum::kRunning)
    {
        PauseProgram();
    }
    else if (mState == OperationalStateEnum::kPaused)
    {
        ResumeProgram();
    }
}

// Track this separately as we need to set some values in the forecast struct.
//
chip::app::Clusters::DeviceEnergyManagement::Structs::SlotStruct::Type sSlots[10];
chip::app::Clusters::DeviceEnergyManagement::Structs::ForecastStruct::Type sForecastStruct;

void DishwasherManager::StartProgram()
{
    mIsProgramSelected = true;

    // 30 minutes in seconds = 1800
    // 60 minutes in seconds = 3600
    // 90 minutes in seconds = 5400
    mRunningTimeRemaining = 1800 + (mMode * 1800);
    mPhase = 0;

    // UpdateCurrentPhase(mPhase);
    // UpdateOperationState(OperationalStateEnum::kRunning);

    // Configure the forecast for the selected program.
    //

    // Get the current time.
    //
    // struct timeval tv_now;
    // gettimeofday(&tv_now, NULL);
    // uint32_t matterEpoch = tv_now.tv_sec;
    // ESP_LOGI(TAG, "Current time: %lu", matterEpoch);

    // Get the current time the CHIP way...
    //
    System::Clock::Microseconds64 utcTime;
    chip::System::SystemClock().GetClock_RealTime(utcTime);

    time_t unixEpoch = std::chrono::duration_cast<chip::System::Clock::Seconds32>(utcTime).count();

    ESP_LOGI(TAG, "Matter time: %llu", unixEpoch);

    // char buf[50];
    // tm calendarTime{};
    // localtime_r(&unixEpoch, &calendarTime);
    // ESP_LOGI(TAG, "The date and time is %s", asctime_r(&calendarTime, buf));

    if (mOptedIntoEnergyManagement)
    {
        mDelayedStartTimeRemaining = 60; // Start in one minute to allow for optimisation
    }
    else
    {
        mDelayedStartTimeRemaining = 0; // Start immediately.
    }

    sForecastStruct.forecastID = 0; // TODO This should change each time the forecast changes.
    sForecastStruct.startTime = unixEpoch + mDelayedStartTimeRemaining;
    sForecastStruct.endTime = unixEpoch + mDelayedStartTimeRemaining + mRunningTimeRemaining;

    if (mOptedIntoEnergyManagement)
    {
        sForecastStruct.earliestStartTime = MakeOptional(unixEpoch);
        sForecastStruct.latestEndTime = MakeOptional(unixEpoch + 86400 /* 24 hours */);
    }
    else
    {
        sForecastStruct.earliestStartTime.ClearValue();
        sForecastStruct.latestEndTime.ClearValue();
    }
    
    sForecastStruct.isPausable = false;         // We cannot pause any of the slots in this forecast.
    sForecastStruct.activeSlotNumber.SetNull(); // TODO Change this accordingly as the program progresses.

    int32_t slot_count = 1;

    sSlots[0].minDuration = 1800;
    sSlots[0].maxDuration = 1800;
    sSlots[0].defaultDuration = 1800;

    // slots[0].elapsedSlotTime = 0;
    // slots[0].remainingSlotTime = 0;

    // slots[0].slotIsPausable.SetValue(true);
    // slots[0].minPauseDuration.SetValue(10);
    // slots[0].maxPauseDuration.SetValue(60);

    sSlots[0].nominalPower.SetValue(3000000);
    sSlots[0].minPower.SetValue(3000000);
    sSlots[0].maxPower.SetValue(3000000);

    if (mMode >= 1)
    {
        sSlots[1].minDuration = 30 * 60;
        sSlots[1].maxDuration = 30 * 60;
        sSlots[1].defaultDuration = 30 * 60;
        sSlots[1].nominalPower.SetValue(3000000);
        sSlots[1].minPower.SetValue(3000000);
        sSlots[1].maxPower.SetValue(3000000);

        slot_count = 2;
    }

    if (mMode >= 2)
    {
        sSlots[2].minDuration = 30 * 60;
        sSlots[2].maxDuration = 30 * 60;
        sSlots[2].defaultDuration = 30 * 60;
        sSlots[2].nominalPower.SetValue(3000000);
        sSlots[2].minPower.SetValue(3000000);
        sSlots[2].maxPower.SetValue(3000000);

        slot_count = 3;
    }

    // sForecastStruct.slots = DataModel::List<DeviceEnergyManagement::Structs::SlotStruct::Type>(sSlots, slot_count);

    // SetForecast();
}

void DishwasherManager::AdjustStartTime(uint32_t new_start_time)
{
    if (mOptedIntoEnergyManagement)
    {
        // TODO If the program has started, we can't adjust the start time.
        //
        sForecastStruct.startTime = new_start_time;
        sForecastStruct.endTime = new_start_time + ((30 + (mMode * 30)) * 60);
        sForecastStruct.forecastUpdateReason = DeviceEnergyManagement::ForecastUpdateReasonEnum::kGridOptimization;

        // Update the delay.
        //
        System::Clock::Microseconds64 utcTime;
        chip::System::SystemClock().GetClock_RealTime(utcTime);

        time_t unixEpoch = std::chrono::duration_cast<chip::System::Clock::Seconds32>(utcTime).count();

        mDelayedStartTimeRemaining = new_start_time - unixEpoch;

        UpdateDishwasherDisplay();

        SetForecast();
    }
}

void DishwasherManager::PauseProgram()
{
    UpdateOperationState(OperationalStateEnum::kPaused);
}

void DishwasherManager::ResumeProgram()
{
    UpdateOperationState(OperationalStateEnum::kRunning);
}

void DishwasherManager::StopProgram()
{
    mIsProgramSelected = false;
    mDelayedStartTimeRemaining = 0;
    mRunningTimeRemaining = 0;
    UpdateCurrentPhase(0);
    UpdateMode(0);
    UpdateOperationState(OperationalStateEnum::kStopped);
    ClearForecast();
}

void DishwasherManager::EndProgram()
{
    // TODO We might want to do other stuff here, like raise a Matter event that the program has ended.
    //
    StopProgram();
}

OperationalStateEnum DishwasherManager::GetOperationalState()
{
    return mState;
}

uint8_t DishwasherManager::GetCurrentMode()
{
    return mMode;
}

void DishwasherManager::UpdateDishwasherDisplay()
{
    ESP_LOGI(TAG, "UpdateDishwasherDisplay called!");

    char *state_text = "";
    char *mode_text = "";
    char *status_text = "";

    switch (mState)
    {
    case OperationalStateEnum::kRunning:
        state_text = "RUNNING";
        break;
    case OperationalStateEnum::kPaused:
        state_text = "PAUSED";
        break;
    case OperationalStateEnum::kStopped:
        state_text = "STOPPED";
        break;
    case OperationalStateEnum::kError:
        // sDishwasherLED.Blink(100);
        //  TODO Blink the three LEDS?!
        break;
    default:
        break;
    }

    ESP_LOGI(TAG, "Time Remaining: %lu", mRunningTimeRemaining);

    char time_buffer[30] = "";

    if (mRunningTimeRemaining > 0)
    {
        sprintf(time_buffer, "%lus", mRunningTimeRemaining);
    }

    char mode_buffer[64];

    DishwasherModeDelegate *delegate = (DishwasherModeDelegate *)DishwasherMode::GetDelegate();

    if (delegate != nullptr)
    {
        MutableCharSpan label(mode_buffer);

        delegate->GetModeLabelByIndex(mMode, label);

        ESP_LOGI(TAG, "Mode Size: %u", label.size());

        mode_text = (char *)malloc(label.size());
        memcpy(mode_text, label.data(), label.size());
        mode_text[label.size()] = '\0';
    }
    else 
    {
        mode_text = "ERR: NO DELEGATE";
    }

    char status_buffer[64];
    char *status_formatted_buffer = NULL;

    if (mState == OperationalStateEnum::kRunning || mState == OperationalStateEnum::kPaused)
    {
        OperationalStateDelegate *operational_state_delegate = (OperationalStateDelegate *)OperationalState::GetDelegate();

        if (operational_state_delegate != nullptr)
        {
            MutableCharSpan label(status_buffer);

            operational_state_delegate->GetOperationalPhaseAtIndex(mPhase, label);

            int length = snprintf((char *)NULL, 0, "%s (%s)", time_buffer, status_buffer) + 1; /* +1 for the null terminator */
            status_formatted_buffer = (char *)malloc(length);
            snprintf(status_formatted_buffer, length, "%s (%s)", time_buffer, status_buffer);

            status_text = status_formatted_buffer;
        }
    }

    StatusDisplayMgr().UpdateDisplay(mIsShowingMenu, mOptedIntoEnergyManagement, mIsProgramSelected, mDelayedStartTimeRemaining, state_text, mode_text, status_text);

    if (status_formatted_buffer != NULL)
    {
        free(status_formatted_buffer);
    }

    free(mode_text);
}

void DishwasherManager::ProgressProgram()
{
    // If there is no program selected, we do nothing.
    //
    if (!mIsProgramSelected)
    {
        return;
    }

    // We might be on a delayed start, so tick down if required.
    //
    if (mDelayedStartTimeRemaining > 0)
    {
        mDelayedStartTimeRemaining--;
        UpdateDishwasherDisplay();
    }
    else
    {
        // If we are stopped, we should start running.
        //
        if (mState == OperationalStateEnum::kStopped)
        {
            mState = OperationalStateEnum::kRunning;
            UpdateOperationState(mState);
        }

        if (mState == OperationalStateEnum::kRunning)
        {
            mRunningTimeRemaining--;

            uint8_t current_phase = 0;

            if (mRunningTimeRemaining <= 0)
            {
                current_phase = 0;
                EndProgram();
            }
            else if (mRunningTimeRemaining < 5)
            {
                current_phase = 4;
            }
            else if (mRunningTimeRemaining < 10)
            {
                current_phase = 3;
            }
            else if (mRunningTimeRemaining < 15)
            {
                current_phase = 2;
            }
            else if (mRunningTimeRemaining < 20)
            {
                current_phase = 1;
            }

            UpdateCurrentPhase(current_phase);
        }
    }
}

static void UpdateOperationalStatePhaseWorkHandler(intptr_t context)
{
    ESP_LOGI(TAG, "UpdateOperationalStatePhaseWorkHandler()");
    DataModel::Nullable<uint8_t> phase = (DataModel::Nullable<uint8_t>)context;
    OperationalState::GetInstance()->SetCurrentPhase(phase);
    DishwasherMgr().UpdateDishwasherDisplay();
}

void DishwasherManager::UpdateCurrentPhase(uint8_t phase)
{
    mPhase = phase;

    // This is one way to perform safe changes to the Matter stack.
    //
    chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateOperationalStatePhaseWorkHandler, mPhase);

    // This is another.
    //
    // chip::DeviceLayer::PlatformMgr().LockChipStack();
    // chip::DeviceLayer::PlatformMgr().UnlockChipStack();
}

static void UpdateOperationalStateWorkHandler(intptr_t context)
{
    ESP_LOGI(TAG, "UpdateOperationalStateWorkHandler()");
    OperationalState::OperationalStateEnum state = (OperationalState::OperationalStateEnum)context;
    OperationalState::GetInstance()->SetOperationalState(to_underlying(state));
    OperationalState::GetInstance()->UpdateCountdownTimeFromDelegate();
    DishwasherMgr().UpdateDishwasherDisplay();
}

void DishwasherManager::UpdateOperationState(OperationalStateEnum state)
{
    mState = state;
    chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateOperationalStateWorkHandler, (uint8_t)mState);
}

void DishwasherManager::UpdateMode(uint8_t mode)
{
    mMode = mode;
    UpdateDishwasherDisplay();
}

static void UpdateDishwasherCurrentModeWorkHandler(intptr_t context)
{
    ESP_LOGI(TAG, "UpdateDishwasherCurrentModeWorkHandler()");
    uint8_t mode = (uint8_t)context;
    DishwasherMode::GetInstance()->UpdateCurrentMode(mode);
    DishwasherMgr().UpdateDishwasherDisplay();
}

void DishwasherManager::SelectNext()
{
    if (!mIsPoweredOn)
    {
        ESP_LOGI(TAG, "Dishwasher is off, cannot handle start");
        return;
    }

    if (mIsShowingMenu)
    {
        mOptedIntoEnergyManagement = !mOptedIntoEnergyManagement;

        chip::DeviceLayer::PlatformMgr().LockChipStack();

        if (mOptedIntoEnergyManagement)
        {
            device_energy_management_delegate.SetOptOutState(OptOutStateEnum::kNoOptOut);
        }
        else
        {
            device_energy_management_delegate.SetOptOutState(OptOutStateEnum::kOptOut);
        }

        chip::DeviceLayer::PlatformMgr().UnlockChipStack();

        ESP_LOGI(TAG, "Opted into energy management: %d", mOptedIntoEnergyManagement);
        UpdateDishwasherDisplay();
    }
    else
    {
        SelectNextMode();
    }
}

void DishwasherManager::SelectPrevious()
{
    if (!mIsPoweredOn)
    {
        ESP_LOGI(TAG, "Dishwasher is off, cannot handle start");
        return;
    }

    if (mIsShowingMenu)
    {
        mOptedIntoEnergyManagement = !mOptedIntoEnergyManagement;

        chip::DeviceLayer::PlatformMgr().LockChipStack();

        if (mOptedIntoEnergyManagement)
        {
            device_energy_management_delegate.SetOptOutState(OptOutStateEnum::kNoOptOut);
        }
        else
        {
            device_energy_management_delegate.SetOptOutState(OptOutStateEnum::kOptOut);
        }

        chip::DeviceLayer::PlatformMgr().UnlockChipStack();

        ESP_LOGI(TAG, "Opted into energy management: %d", mOptedIntoEnergyManagement);
        UpdateDishwasherDisplay();
    }
    else
    {
        SelectPreviousMode();
    }
}

void DishwasherManager::HandleWheelClicked()
{
    if (!mIsPoweredOn)
    {
        ESP_LOGI(TAG, "Dishwasher is off, cannot handle wheel click");
        return;
    }

    if (mIsProgramSelected)
    {
        StopProgram();
        UpdateDishwasherDisplay();
    }
    else
    {
        mIsShowingMenu = !mIsShowingMenu;
        UpdateDishwasherDisplay();
    }
}

void DishwasherManager::SelectNextMode()
{
    if (!mIsPoweredOn)
    {
        ESP_LOGI(TAG, "Dishwasher is off, cannot change mode");
        return;
    }

    ESP_LOGI(TAG, "SelectNextMode called!");

    if (mState != OperationalStateEnum::kStopped)
    {
        ESP_LOGI(TAG, "Mode can only be changed when dishwasher is stopped!");
        return;
    }

    mMode++;

    // Roll over if we reach the end
    //
    if (mMode > 2)
    {
        mMode = 0;
    }

    ESP_LOGI(TAG, "Selected Mode: %d", mMode);

    chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateDishwasherCurrentModeWorkHandler, mMode);
}

void DishwasherManager::SelectPreviousMode()
{
    if (!mIsPoweredOn)
    {
        ESP_LOGI(TAG, "Dishwasher is off, cannot change mode");
        return;
    }

    uint8_t current_mode = DishwasherMode::GetInstance()->GetCurrentMode();

    ESP_LOGI(TAG, "Current Mode: %d", current_mode);

    // Roll over if we reach the start
    //
    if (mMode == 0)
    {
        mMode = 2;
    }
    else
    {
        mMode--;
    }

    ESP_LOGI(TAG, "Selected Mode: %d", mMode);

    chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateDishwasherCurrentModeWorkHandler, mMode);
}

static void UpdateForecastWorkHandler(intptr_t context)
{
    ESP_LOGI(TAG, "UpdateForecastWorkHandler()");
    device_energy_management_delegate.SetForecast(DataModel::MakeNullable(sForecastStruct));
}

void DishwasherManager::SetForecast()
{
    ESP_LOGI(TAG, "DishwasherManager::SetForecast()");

    // Use the ScheduleWork method.
    //
    chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateForecastWorkHandler, mMode);

    // Another approach is locking the stack manually.
    //
    // chip::DeviceLayer::PlatformMgr().LockChipStack();
    // device_energy_management_delegate.SetForecast(DataModel::MakeNullable(sForecastStruct));
    // chip::DeviceLayer::PlatformMgr().UnlockChipStack();
}

void DishwasherManager::ClearForecast()
{
    ESP_LOGI(TAG, "DishwasherManager::ClearForecast()");

    sForecastStruct.startTime = 0;
    sForecastStruct.endTime = 0;
    sForecastStruct.earliestStartTime.ClearValue();
    sForecastStruct.latestEndTime.ClearValue();
    sForecastStruct.isPausable = false;
    sForecastStruct.activeSlotNumber.SetNull();
    sForecastStruct.slots = DataModel::List<const DeviceEnergyManagement::Structs::SlotStruct::Type>();

    SetForecast();
}