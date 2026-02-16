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

    void UpdateDisplay(bool showingMenu, bool hasOptedIn, bool isProgramSelected, uint8_t startsInMinutes, uint8_t runningTimeRemaining, char state_text[32], char mode_text[10]);

    void ShowResetOptions();
    void HideResetOptions();

    void SetCommissioningCode(char *data, size_t size);

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
    lv_obj_t *mStartsInLabel;
    lv_obj_t *mMenuButtonLabel;
    lv_obj_t *mMenuHeaderLabel;
    lv_obj_t *mEnergyManagementOptOutLabel;
    lv_obj_t *mEnergyManagementOptInLabel;
    lv_obj_t *mOnOffButtonLabel;

    bool mIsShowingMenu = false;
    uint8_t mTimeRemaining = 0;
    char mStateText[32];
    char mModeText[15];
};

inline StatusDisplay & StatusDisplayMgr(void)
{
    return StatusDisplay::sStatusDisplay;
}