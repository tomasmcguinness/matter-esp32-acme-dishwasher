#include "mode_selector.h"
#include <esp_err.h>
#include <esp_log.h>
#include <string.h>

#include <driver/gpio.h>
#include "iot_button.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "dishwasher_manager.h"

static const char *TAG = "mode_selector";

ModeSelector ModeSelector::sModeSelector;

static void up_button_single_click_cb(void *args, void *user_data)
{
    ESP_LOGI(TAG, "Up Clicked");
    DishwasherMgr().SelectNext();
}

static void down_button_single_click_cb(void *args, void *user_data)
{
    ESP_LOGI(TAG, "Down Clicked");
    DishwasherMgr().SelectPrevious();
}

esp_err_t ModeSelector::Init()
{
    ESP_LOGI(TAG, "ModelSelector::Init()");

    button_config_t onoff_config;
    memset(&onoff_config, 0, sizeof(button_config_t));

    onoff_config.type = BUTTON_TYPE_GPIO;
    onoff_config.gpio_button_config.gpio_num = GPIO_NUM_6;
    onoff_config.gpio_button_config.active_level = 0;

    button_handle_t onoff_handle = iot_button_create(&onoff_config);

    ESP_ERROR_CHECK(iot_button_register_cb(onoff_handle, BUTTON_SINGLE_CLICK, up_button_single_click_cb, NULL));

    button_config_t start_config;
    memset(&start_config, 0, sizeof(button_config_t));

    start_config.type = BUTTON_TYPE_GPIO;
    start_config.gpio_button_config.gpio_num = GPIO_NUM_4;
    start_config.gpio_button_config.active_level = 0;

    button_handle_t start_handle = iot_button_create(&start_config);

    ESP_ERROR_CHECK(iot_button_register_cb(start_handle, BUTTON_SINGLE_CLICK, down_button_single_click_cb, NULL));

    ESP_LOGI(TAG, "mode_selector initialised");

    return ESP_OK;
}