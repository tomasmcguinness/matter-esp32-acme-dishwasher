#include "mode_selector.h"
#include <esp_err.h>
#include <esp_log.h>
#include <string.h>

#include <driver/pulse_cnt.h>
#include <driver/gpio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "dishwasher_manager.h"

#define EXAMPLE_PCNT_HIGH_LIMIT 100
#define EXAMPLE_PCNT_LOW_LIMIT -100

static const char *TAG = "mode_selector";

static pcnt_unit_handle_t pcnt_unit = NULL;
static QueueHandle_t gpio_pulse_evt_queue = NULL;

static int current_pulse_count = 0;
static int pulse_count = 0;
static int event_count = 0;

ModeSelector ModeSelector::sModeSelector;

#define EXAMPLE_PCNT_HIGH_LIMIT 100
#define EXAMPLE_PCNT_LOW_LIMIT -100

static void pulse_counter_monitor_task(void *arg)
{
    while (1)
    {
        //ESP_LOGD(TAG, "Waiting for event on pulse_evt_queue");

        if (xQueueReceive(gpio_pulse_evt_queue, &event_count, pdMS_TO_TICKS(200)))
        {
            //ESP_LOGD(TAG, "Watch point event, count: %d", event_count);
        }
        else
        {
            ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
            //ESP_LOGD(TAG, "Current pulse count: %d", pulse_count);

            if (pulse_count != current_pulse_count)
            {
                int pulse_difference = pulse_count - current_pulse_count;

                current_pulse_count = pulse_count;

                // ESP_LOGI(TAG, "Pulse Difference: %d", pulse_difference);

                if (pulse_difference < 0)
                {
                    DishwasherMgr().SelectNext();
                }
                else
                {
                    DishwasherMgr().SelectPrevious();
                }
            }
        }
    }
}

esp_err_t ModeSelector::Init()
{
    ESP_LOGI(TAG, "install pcnt unit");

    pcnt_unit_config_t unit_config = {
        .low_limit = EXAMPLE_PCNT_LOW_LIMIT,
        .high_limit = EXAMPLE_PCNT_HIGH_LIMIT,
        .intr_priority = 1};

    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    ESP_LOGI(TAG, "set glitch filter");
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    ESP_LOGI(TAG, "install pcnt channels");
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = GPIO_NUM_18,
        .level_gpio_num = GPIO_NUM_20,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = GPIO_NUM_20,
        .level_gpio_num = GPIO_NUM_18,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    ESP_LOGI(TAG, "set edge and level actions for pcnt channels");
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_LOGI(TAG, "add watch points and register callbacks");
    int watch_points[] = {EXAMPLE_PCNT_LOW_LIMIT, -50, 0, 50, EXAMPLE_PCNT_HIGH_LIMIT};
    for (size_t i = 0; i < sizeof(watch_points) / sizeof(watch_points[0]); i++)
    {
        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, watch_points[i]));
    }
    pcnt_event_callbacks_t cbs = {
        .on_reach = NULL,
    };

    gpio_pulse_evt_queue = xQueueCreate(10, sizeof(int));
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, gpio_pulse_evt_queue));

    ESP_LOGI(TAG, "enable pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_LOGI(TAG, "clear pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_LOGI(TAG, "start pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    xTaskCreate(pulse_counter_monitor_task, "mode_selector_task", 2048, NULL, tskIDLE_PRIORITY, NULL);

    ESP_LOGI(TAG, "mode_selector initialised");

    return ESP_OK;
}