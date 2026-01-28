#include <stdio.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "status_display.h"

#include "driver/i2c_master.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"

#include "esp_lcd_panel_ssd1681.h"

#include "lvgl.h"

static const char *TAG = "status_display";

#define I2C_BUS_PORT 0
#define LCD_HOST SPI2_HOST

#define EXAMPLE_LCD_CMD_BITS 8
#define EXAMPLE_LCD_PARAM_BITS 8

#define LCD_H_RES 400
#define LCD_V_RES 300

#define PIN_NUM_MOSI 11
#define PIN_NUM_MISO 13
#define PIN_NUM_SCLK 12
#define PIN_NUM_CS 10
#define PIN_NUM_DC 46
#define PIN_NUM_RST 47
#define PIN_NUM_BUSY 48
#define PIN_NUM_INDICATOR_LED 41
#define PIN_NUM_LCD_POWER 7

StatusDisplay StatusDisplay::sStatusDisplay;

esp_err_t StatusDisplay::Init()
{
    ESP_LOGI(TAG, "StatusDisplay::Init()");

    ESP_LOGI(TAG, "Initialize SPI bus");

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SOC_SPI_MAXIMUM_BUFFER_SIZE,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_NUM_CS,
        .dc_gpio_num = PIN_NUM_DC,
        .spi_mode = 0,
        .pclk_hz = 20 * 1000 * 1000,
        .trans_queue_depth = 7,
        .on_color_trans_done = NULL,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install SSD1683 panel driver");
    esp_lcd_ssd1681_config_t epaper_ssd1681_config = {
        .busy_gpio_num = PIN_NUM_BUSY,
        .non_copy_mode = false,
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .flags = {
            .reset_active_high = true,
        },
        .vendor_config = &epaper_ssd1681_config};
    // gpio_install_isr_service(0);
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1681(io_handle, &panel_config, &mPanelHandle));

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = ((1ULL << PIN_NUM_INDICATOR_LED) | (1ULL << PIN_NUM_LCD_POWER));
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_set_level((gpio_num_t)PIN_NUM_LCD_POWER, true);
    ESP_LOGI(TAG, "Applied power to display");

    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Performing reset...");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(mPanelHandle));

    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Performing init...");
    ESP_ERROR_CHECK(esp_lcd_panel_init(mPanelHandle));

    vTaskDelay(100 / portTICK_PERIOD_MS);

    // --- Turn on display
    // ESP_LOGI(TAG, "Turning e-Paper display on...");
    // ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(mPanelHandle, true));
    // vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Initialize LVGL");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    ESP_LOGI(TAG, "Create LVGL Display");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = mPanelHandle,
        .buffer_size = LCD_H_RES * LCD_V_RES,
        .double_buffer = false,
        .trans_size = 1024,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_I1,
        .flags = {.buff_dma = false, .buff_spiram = true, .full_refresh = true}};

    mDisplayHandle = lvgl_port_add_disp(&disp_cfg);

    ESP_LOGI(TAG, "Create LVGL Screen");

    lv_obj_t *scr = lv_scr_act();

    LV_FONT_DECLARE(lv_font_montserrat_48);

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_font(&style, &lv_font_montserrat_48);

    mQRCode = lv_qrcode_create(scr);
    lv_qrcode_set_size(mQRCode, 200);
    lv_qrcode_set_dark_color(mQRCode, lv_color_hex3(0xFF));
    lv_qrcode_set_light_color(mQRCode, lv_color_hex3(0x00));
    lv_obj_align(mQRCode, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(mQRCode, LV_OBJ_FLAG_HIDDEN);

    mStateLabel = lv_label_create(scr);

    lv_label_set_text(mStateLabel, "STOPPED"); // TODO Get this default from the DishwasherManager
    lv_obj_set_width(mStateLabel, 400);
    lv_obj_align(mStateLabel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(mStateLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mStateLabel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(mStateLabel, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_add_style(mStateLabel, &style, LV_PART_MAIN);
    lv_obj_set_style_pad_left(mStateLabel, 10, LV_PART_MAIN);
    lv_obj_add_flag(mStateLabel, LV_OBJ_FLAG_HIDDEN);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    mSelectedProgramLabel = lv_label_create(scr);
    lv_label_set_text(mSelectedProgramLabel, "Selected Program");
    lv_obj_set_width(mSelectedProgramLabel, 400);
    lv_obj_align(mSelectedProgramLabel, LV_ALIGN_LEFT_MID, 0, -30);
    //lv_obj_add_flag(mSelectedProgramLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mSelectedProgramLabel, LV_OBJ_FLAG_HIDDEN);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    mModeLabel = lv_label_create(scr);
    lv_label_set_text(mModeLabel, "Eco 50°"); // TODO Get this default from the DishwasherManager
    lv_obj_set_width(mModeLabel, 400);
    lv_obj_align(mModeLabel, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_color(mModeLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_style(mModeLabel, &style, LV_PART_MAIN);
    lv_obj_set_style_pad_left(mModeLabel, 20, LV_PART_MAIN);
    lv_obj_add_flag(mModeLabel, LV_OBJ_FLAG_HIDDEN);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    mStatusLabel = lv_label_create(scr);

    lv_label_set_text(mStatusLabel, "");
    lv_obj_set_width(mStatusLabel, 400);
    lv_obj_align(mStatusLabel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_color(mStatusLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_style(mStatusLabel, &style, LV_PART_MAIN);
    lv_obj_set_style_pad_left(mStatusLabel, 20, LV_PART_MAIN);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    mResetMessageLabel = lv_label_create(scr);

    lv_label_set_text(mResetMessageLabel, "RESET DEVICE?");
    lv_obj_set_width(mResetMessageLabel, 400);
    lv_obj_add_flag(mResetMessageLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(mResetMessageLabel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_color(mResetMessageLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_style(mResetMessageLabel, &style, LV_PART_MAIN);

    vTaskDelay(10 / portTICK_PERIOD_MS);
    
    mYesButtonLabel = lv_label_create(scr);

    lv_label_set_text(mYesButtonLabel, "YES");
    lv_obj_set_width(mYesButtonLabel, 400);
    lv_obj_add_flag(mYesButtonLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_align(mYesButtonLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(mYesButtonLabel, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_style(mYesButtonLabel, &style, LV_PART_MAIN);
    lv_obj_set_style_text_color(mYesButtonLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    mNoButtonLabel = lv_label_create(scr);

    lv_label_set_text(mNoButtonLabel, "NO");
    lv_obj_set_width(mNoButtonLabel, 400);
    lv_obj_add_flag(mNoButtonLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_align(mNoButtonLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(mNoButtonLabel, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_style(mNoButtonLabel, &style, LV_PART_MAIN);
    lv_obj_set_style_text_color(mNoButtonLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    mStartsInLabel = lv_label_create(scr);

    lv_label_set_text(mStartsInLabel, "");
    lv_obj_set_width(mStartsInLabel, 400);
    lv_obj_add_flag(mStartsInLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_align(mStartsInLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(mStartsInLabel, LV_ALIGN_CENTER, 0, 0);

    mMenuButtonLabel = lv_label_create(scr);

    lv_label_set_text(mMenuButtonLabel, "MENU");
    lv_obj_set_width(mMenuButtonLabel, 50);
    lv_obj_set_style_text_align(mMenuButtonLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(mMenuButtonLabel, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_color(mMenuButtonLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    mMenuHeaderLabel = lv_label_create(scr);

    lv_label_set_text(mMenuHeaderLabel, "MENU");
    lv_obj_set_width(mMenuHeaderLabel, 400);
    lv_obj_add_flag(mMenuHeaderLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_align(mMenuHeaderLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(mMenuHeaderLabel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_color(mMenuHeaderLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_style(mMenuHeaderLabel, &style, LV_PART_MAIN);

    mEnergyManagementOptOutLabel = lv_label_create(scr);

    lv_label_set_text(mEnergyManagementOptOutLabel, "Opt Out");
    lv_obj_add_flag(mEnergyManagementOptOutLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(mEnergyManagementOptOutLabel, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_align(mEnergyManagementOptOutLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_bg_color(mEnergyManagementOptOutLabel, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mEnergyManagementOptOutLabel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(mEnergyManagementOptOutLabel, lv_color_hex(0xffffff), LV_PART_MAIN);

    mEnergyManagementOptInLabel = lv_label_create(scr);

    lv_label_set_text(mEnergyManagementOptInLabel, "Opt In");
    lv_obj_add_flag(mEnergyManagementOptInLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(mEnergyManagementOptInLabel, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_align(mEnergyManagementOptInLabel, LV_TEXT_ALIGN_RIGHT, 0);

    vTaskDelay(250 / portTICK_PERIOD_MS);

    ESP_ERROR_CHECK(epaper_panel_refresh_screen(mPanelHandle));

    ESP_LOGI(TAG, "StatusDisplay::Init() finished");

    return ESP_OK;
}

void StatusDisplay::TurnOn()
{
    ESP_LOGI(TAG, "Turning display on");

    gpio_set_level((gpio_num_t)PIN_NUM_INDICATOR_LED, true);
    ESP_LOGI(TAG, "Applied power to LED");

    lv_obj_clear_flag(mStateLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(mModeLabel, LV_OBJ_FLAG_HIDDEN);

    vTaskDelay(250 / portTICK_PERIOD_MS);

    ESP_ERROR_CHECK(epaper_panel_refresh_screen(mPanelHandle));
}

void StatusDisplay::TurnOff()
{
    ESP_LOGI(TAG, "Turning display off");

    gpio_set_level((gpio_num_t)PIN_NUM_INDICATOR_LED, false);
    ESP_LOGI(TAG, "Removed power to LED");

    lv_obj_add_flag(mStateLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mModeLabel, LV_OBJ_FLAG_HIDDEN);

    vTaskDelay(250 / portTICK_PERIOD_MS);

    ESP_ERROR_CHECK(epaper_panel_refresh_screen(mPanelHandle));
}

void StatusDisplay::UpdateDisplay(bool showingMenu, bool hasOptedIn, bool isProgramSelected, uint8_t startsInMinutes, uint8_t timeRemaining, const char *state_text, const char *mode_text)
{
    ESP_LOGI(TAG, "Updating the display");

    ESP_LOGI(TAG, "showingMenu: [%d]", showingMenu);
    ESP_LOGI(TAG, "hasOptedIn: [%d]", hasOptedIn);
    ESP_LOGI(TAG, "isProgramSelected: [%d]", isProgramSelected);
    ESP_LOGI(TAG, "startsIn: [%u]", startsInMinutes);
    ESP_LOGI(TAG, "timeRemaining: [%u]", timeRemaining);
    ESP_LOGI(TAG, "state_text: [%s]", state_text);
    ESP_LOGI(TAG, "mode_text: [%s]", mode_text);
    // ESP_LOGI(TAG, "status_text: [%s]", status_text);

    bool shouldRefresh = false;

    bool hasMenuChanged = mIsShowingMenu != showingMenu;
    bool hasTimeRemainingChanged = mTimeRemaining != timeRemaining;
    bool hasModeTextChanged = strcmp(mModeText, mode_text) != 0;

    shouldRefresh = hasMenuChanged || hasTimeRemainingChanged || hasModeTextChanged;

    ESP_LOGI(TAG, "hasMenuChanged: [%d]", hasMenuChanged);
    ESP_LOGI(TAG, "hasTimeRemainingChanged: [%d]", hasTimeRemainingChanged);
    ESP_LOGI(TAG, "hasModeTextChanged: [%d]", hasModeTextChanged);
    ESP_LOGI(TAG, "shouldRefresh: [%d]", shouldRefresh);

    if (!shouldRefresh)
    {
        return;
    }

    if (showingMenu)
    {
        ESP_LOGI(TAG, "Showing the menu");

        lv_label_set_text(mMenuButtonLabel, "EXIT");
        lv_obj_clear_flag(mMenuHeaderLabel, LV_OBJ_FLAG_HIDDEN);

        if (hasOptedIn)
        {
            // Remove background from Opt Out
            lv_obj_set_style_bg_color(mEnergyManagementOptOutLabel, lv_color_hex(0xffffff), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(mEnergyManagementOptOutLabel, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(mEnergyManagementOptOutLabel, lv_color_hex(0x000000), LV_PART_MAIN);

            lv_obj_set_style_bg_color(mEnergyManagementOptInLabel, lv_color_hex(0x000000), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(mEnergyManagementOptInLabel, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(mEnergyManagementOptInLabel, lv_color_hex(0xffffff), LV_PART_MAIN);
        }
        else
        {
            // Remove background from Opt In
            lv_obj_set_style_bg_color(mEnergyManagementOptInLabel, lv_color_hex(0xffffff), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(mEnergyManagementOptInLabel, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(mEnergyManagementOptInLabel, lv_color_hex(0x000000), LV_PART_MAIN);

            lv_obj_set_style_bg_color(mEnergyManagementOptOutLabel, lv_color_hex(0x000000), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(mEnergyManagementOptOutLabel, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(mEnergyManagementOptOutLabel, lv_color_hex(0xffffff), LV_PART_MAIN);
        }

        lv_obj_clear_flag(mEnergyManagementOptOutLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(mEnergyManagementOptInLabel, LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_flag(mStateLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mModeLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mStatusLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mStartsInLabel, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        // The standard screen (menu closed)
        //
        lv_label_set_text(mMenuButtonLabel, "MENU");
        lv_obj_clear_flag(mMenuButtonLabel, LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_flag(mMenuHeaderLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mEnergyManagementOptOutLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mEnergyManagementOptInLabel, LV_OBJ_FLAG_HIDDEN);

        if (isProgramSelected)
        {
            // If there a delayed start?
            //
            if (startsInMinutes > 0)
            {
                lv_label_set_text(mMenuButtonLabel, "CANCEL");
                lv_obj_clear_flag(mMenuButtonLabel, LV_OBJ_FLAG_HIDDEN);

                lv_obj_add_flag(mStateLabel, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(mModeLabel, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(mStatusLabel, LV_OBJ_FLAG_HIDDEN);

                char *starts_in_formatted_buffer = (char *)malloc(22);
                snprintf(starts_in_formatted_buffer, 128, "Starts in %u minutes", startsInMinutes);

                lv_label_set_text(mStartsInLabel, starts_in_formatted_buffer);
                lv_obj_clear_flag(mStartsInLabel, LV_OBJ_FLAG_HIDDEN);

                free(starts_in_formatted_buffer);
            }
            else
            {
                lv_obj_add_flag(mMenuButtonLabel, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(mStartsInLabel, LV_OBJ_FLAG_HIDDEN);

                lv_obj_clear_flag(mStateLabel, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(mModeLabel, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(mStatusLabel, LV_OBJ_FLAG_HIDDEN);

                lv_label_set_text(mStateLabel, state_text);
                lv_label_set_text(mModeLabel, mode_text);

                char *time_remaining_formatted_buffer = (char *)malloc(15);
                if (timeRemaining > 1)
                {
                    snprintf(time_remaining_formatted_buffer, 15, "%u minutes", timeRemaining);
                }
                else if (timeRemaining == 1)
                {
                    snprintf(time_remaining_formatted_buffer, 15, "%u minute", timeRemaining);
                }
                else
                {
                    snprintf(time_remaining_formatted_buffer, 15, "< %u minute", timeRemaining);
                }

                lv_obj_clear_flag(mStatusLabel, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(mStatusLabel, time_remaining_formatted_buffer);

                free(time_remaining_formatted_buffer);
            }
        }
        else
        {
            lv_obj_clear_flag(mStateLabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(mModeLabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(mStatusLabel, LV_OBJ_FLAG_HIDDEN);

            lv_obj_add_flag(mStartsInLabel, LV_OBJ_FLAG_HIDDEN);

            lv_label_set_text(mStateLabel, state_text);
            lv_label_set_text(mModeLabel, mode_text);
            // lv_label_set_text(mStatusLabel, status_text);
        }
    }

    mIsShowingMenu = showingMenu;
    mTimeRemaining = timeRemaining;
    mModeText = (char *)mode_text;

    if (shouldRefresh)
    {
        vTaskDelay(250 / portTICK_PERIOD_MS);
        ESP_ERROR_CHECK(epaper_panel_refresh_screen(mPanelHandle));
    }
}

void StatusDisplay::ShowResetOptions()
{
    ESP_LOGI(TAG, "Showing reset options");

    lv_obj_add_flag(mStateLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mModeLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mStatusLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mMenuButtonLabel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_remove_flag(mResetMessageLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(mYesButtonLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(mNoButtonLabel, LV_OBJ_FLAG_HIDDEN);

    vTaskDelay(250 / portTICK_PERIOD_MS);

    ESP_ERROR_CHECK(epaper_panel_refresh_screen(mPanelHandle));
}

void StatusDisplay::HideResetOptions()
{
    ESP_LOGI(TAG, "Hide reset options");

    lv_obj_remove_flag(mStateLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(mModeLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(mStatusLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(mMenuButtonLabel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(mResetMessageLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mYesButtonLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mNoButtonLabel, LV_OBJ_FLAG_HIDDEN);

    vTaskDelay(500 / portTICK_PERIOD_MS);

    ESP_ERROR_CHECK(epaper_panel_refresh_screen(mPanelHandle));
}

void StatusDisplay::SetCommissioningCode(char *qrCode, size_t size)
{
    ESP_LOGI(TAG, "Set QR CODE [%d] %s", size, qrCode);
    mCommissioningCode = (char *)calloc(1, size + 1); // Allow for null.
    memcpy(mCommissioningCode, qrCode, size);
}

void StatusDisplay::ShowCommissioningCode()
{
    ESP_LOGI(TAG, "ShowCommissioningCode()");

    lv_obj_add_flag(mStateLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mModeLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mStatusLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mMenuButtonLabel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_remove_flag(mQRCode, LV_OBJ_FLAG_HIDDEN);
    lv_qrcode_update(mQRCode, mCommissioningCode, strlen(mCommissioningCode));

    vTaskDelay(500 / portTICK_PERIOD_MS);

    ESP_ERROR_CHECK(epaper_panel_refresh_screen(mPanelHandle));

    free(mCommissioningCode);
}

void StatusDisplay::HideCommissioningCode()
{
    lv_obj_remove_flag(mStateLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(mModeLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(mStatusLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(mMenuButtonLabel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(mQRCode, LV_OBJ_FLAG_HIDDEN);

    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_ERROR_CHECK(epaper_panel_refresh_screen(mPanelHandle));
}