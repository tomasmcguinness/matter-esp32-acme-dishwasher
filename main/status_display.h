#include <stdio.h>
#include "driver/gpio.h"
#include "lvgl.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include <esp_matter.h>

#include <inttypes.h>

enum State {
  STOPPED,
  RUNNING,
  PAUSED
}; 

class StatusDisplay
{
public:
    StatusDisplay() 
    {
        ESP_LOGI("StatusDisplay", "StatusDisplay::StatusDisplay()");
    }

    esp_err_t Init();

    void TurnOn();
    void TurnOff();

    void UpdateDisplay(bool showingMenu, bool hasOptedIn, bool isProgramSelected, int32_t startsInMinutes, int32_t runningTimeRemaining, const char *state_text, const char *mode_text);
    //void UpdateDisplay(bool showingMenu, bool hasOptedIn, bool programSelected, int32_t startsIn, const char *state_text, const char *mode_text, const char *status_text);

    void ShowResetOptions();
    void HideResetOptions();

    void SetCommissioningCode(char *data, size_t size);
    void ShowCommissioningCode();
    void HideCommissioningCode();

private:
    friend StatusDisplay & StatusDisplayMgr(void);
    static StatusDisplay sStatusDisplay;

    bool mScreenOn = false;

    char *mCommissioningCode;

    lv_disp_t *mDisplayHandle;
    esp_lcd_panel_handle_t mPanelHandle;

    lv_obj_t *mQRCode;
    lv_obj_t *mStatusLabel;
    lv_obj_t *mStateLabel;
    lv_obj_t *mSelectedProgramLabel;
    lv_obj_t *mModeLabel;
    lv_obj_t *mResetMessageLabel;
    lv_obj_t *mYesButtonLabel;
    lv_obj_t *mNoButtonLabel;
    lv_obj_t *mStartsInLabel;
    lv_obj_t *mMenuButtonLabel;
    lv_obj_t *mMenuHeaderLabel;
    lv_obj_t *mEnergyManagementOptOutLabel;
    lv_obj_t *mEnergyManagementOptInLabel;

    bool mIsShowingMenu = false;
    uint32_t mRunningTimeRemaining = 0;
    char *mModeText = NULL;
};

inline StatusDisplay & StatusDisplayMgr(void)
{
    return StatusDisplay::sStatusDisplay;
}