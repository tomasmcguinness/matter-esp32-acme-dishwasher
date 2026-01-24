/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "esp_attr.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ssd1681.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ssd1681_commands.h"

static const char *TAG = "lcd_panel.epaper";

typedef struct
{
    esp_lcd_epaper_panel_cb_t callback_ptr;
    void *args;
} epaper_panel_callback_t;

typedef struct
{
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    // --- Normal configurations
    // Configurations from panel_dev_config
    int reset_gpio_num;
    bool reset_level;
    // Configurations from epaper_ssd1681_conf
    int busy_gpio_num;
    bool full_refresh;
    // Configurations from interface functions
    int gap_x;
    int gap_y;
    // Configurations from e-Paper specific public functions
    epaper_panel_callback_t epaper_refresh_done_isr_callback;
    esp_lcd_ssd1681_bitmap_color_t bitmap_color;
    // --- Associated configurations
    // SHOULD NOT modify directly
    // in order to avoid going into undefined state
    bool _non_copy_mode;
    bool _mirror_y;
    bool _swap_xy;
    // --- Other private fields
    bool _mirror_x;
    uint8_t *_framebuffer;
    bool _invert_color;
} epaper_panel_t;

// --- Utility functions
static esp_err_t process_bitmap(esp_lcd_panel_t *panel, int len_x, int len_y, int buffer_size, const void *color_data);
static esp_err_t panel_epaper_wait_busy(esp_lcd_panel_t *panel);
static void epaper_driver_gpio_isr_handler(void *arg);
static esp_err_t panel_epaper_set_vram(esp_lcd_panel_io_handle_t io, uint8_t *bw_bitmap, uint8_t *red_bitmap, size_t size);
static esp_err_t epaper_panel_del(esp_lcd_panel_t *panel);
static esp_err_t epaper_panel_reset(esp_lcd_panel_t *panel);
static esp_err_t epaper_panel_init(esp_lcd_panel_t *panel);
static esp_err_t epaper_panel_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t epaper_panel_disp_on_off(esp_lcd_panel_t *panel, bool on_off);

static void epaper_driver_gpio_isr_handler(void *arg)
{
    // ESP_LOGI(TAG, "Busy has been cleared");

    epaper_panel_t *epaper_panel = arg;

    // --- Disable ISR handling
    gpio_intr_disable(epaper_panel->busy_gpio_num);

    // --- Call user callback func
    if (epaper_panel->epaper_refresh_done_isr_callback.callback_ptr)
    {
        (epaper_panel->epaper_refresh_done_isr_callback.callback_ptr)(&(epaper_panel->base), NULL, epaper_panel->epaper_refresh_done_isr_callback.args);
    }
}

esp_err_t epaper_panel_register_event_callbacks(esp_lcd_panel_t *panel, epaper_panel_callbacks_t *cbs, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG, "panel handler is NULL");
    ESP_RETURN_ON_FALSE(cbs, ESP_ERR_INVALID_ARG, TAG, "cbs is NULL");
    epaper_panel_t *epaper_panel = __containerof(panel, epaper_panel_t, base);
    (epaper_panel->epaper_refresh_done_isr_callback).callback_ptr = cbs->on_epaper_refresh_done;
    (epaper_panel->epaper_refresh_done_isr_callback).args = user_ctx;
    return ESP_OK;
}

static esp_err_t panel_epaper_wait_busy(esp_lcd_panel_t *panel)
{
    ESP_LOGI(TAG, "Waiting until display is no longer busy");

    int wait_count = 0;

    epaper_panel_t *epaper_panel = __containerof(panel, epaper_panel_t, base);
    while (gpio_get_level(epaper_panel->busy_gpio_num))
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_count++;

        if (wait_count >= 20)
        {
            ESP_LOGI(TAG, "Can't wait any longer!");
            return ESP_OK;
        }
    }

    ESP_LOGI(TAG, "Display is no longer busy!");

    return ESP_OK;
}

esp_err_t panel_epaper_set_vram(esp_lcd_panel_io_handle_t io, uint8_t *bw_bitmap, uint8_t *red_bitmap, size_t size)
{
    if (bw_bitmap && (size > 0))
    {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(io, SSD1681_CMD_WRITE_BLACK_VRAM, bw_bitmap, size), TAG, "data bw_bitmap err");
    }
    return ESP_OK;
}

esp_err_t epaper_panel_refresh_screen(esp_lcd_panel_t *panel)
{
    ESP_LOGI(TAG, "epaper_panel_refresh_screen");

    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG, "panel handler is NULL");
    epaper_panel_t *epaper_panel = __containerof(panel, epaper_panel_t, base);

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(epaper_panel->io, SSD1681_CMD_SET_DISP_UPDATE_CTRL, (uint8_t[]){0xC7}, 1), TAG, "SSD1681_CMD_SET_DISP_UPDATE_CTRL err");

    // gpio_intr_enable(epaper_panel->busy_gpio_num);

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(epaper_panel->io, SSD1681_CMD_ACTIVE_DISP_UPDATE_SEQ, NULL, 0), TAG, "SSD1681_CMD_ACTIVE_DISP_UPDATE_SEQ err");

    ESP_LOGI(TAG, "epaper_panel_refresh_screen - command sent");

    return ESP_OK;
}

esp_err_t
esp_lcd_new_panel_ssd1681(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *const panel_dev_config, esp_lcd_panel_handle_t *const ret_panel)
{
#if CONFIG_LCD_ENABLE_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "1 or more args is NULL");
    esp_lcd_ssd1681_config_t *epaper_ssd1681_conf = panel_dev_config->vendor_config;
    esp_err_t ret = ESP_OK;
    // --- Allocate epaper_panel memory on HEAP
    epaper_panel_t *epaper_panel = NULL;
    epaper_panel = calloc(1, sizeof(epaper_panel_t));
    ESP_GOTO_ON_FALSE(epaper_panel, ESP_ERR_NO_MEM, err, TAG, "no mem for epaper panel");

    // --- Construct panel & implement interface
    // defaults
    epaper_panel->_invert_color = false;
    epaper_panel->_swap_xy = false;
    epaper_panel->_mirror_x = false;
    epaper_panel->_mirror_y = false;
    epaper_panel->_framebuffer = NULL;
    epaper_panel->gap_x = 0;
    epaper_panel->gap_y = 0;
    epaper_panel->bitmap_color = SSD1681_EPAPER_BITMAP_BLACK;
    epaper_panel->full_refresh = true;
    // configurations
    epaper_panel->io = io;
    epaper_panel->reset_gpio_num = panel_dev_config->reset_gpio_num;
    epaper_panel->busy_gpio_num = epaper_ssd1681_conf->busy_gpio_num;
    epaper_panel->reset_level = panel_dev_config->flags.reset_active_high;
    epaper_panel->_non_copy_mode = epaper_ssd1681_conf->non_copy_mode;
    // functions
    epaper_panel->base.del = epaper_panel_del;
    epaper_panel->base.reset = epaper_panel_reset;
    epaper_panel->base.init = epaper_panel_init;
    epaper_panel->base.draw_bitmap = epaper_panel_draw_bitmap;
    epaper_panel->base.disp_on_off = epaper_panel_disp_on_off;
    *ret_panel = &(epaper_panel->base);
    // --- Init framebuffer
    if (!(epaper_panel->_non_copy_mode))
    {
        epaper_panel->_framebuffer = heap_caps_malloc(400 * 300 / 8, MALLOC_CAP_DMA);
        ESP_RETURN_ON_FALSE(epaper_panel->_framebuffer, ESP_ERR_NO_MEM, TAG, "epaper_panel_draw_bitmap allocating buffer memory err");
    }
    // --- Init GPIO
    // init RST GPIO
    if (epaper_panel->reset_gpio_num >= 0)
    {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "Configure GPIO for RST line err");
    }
    // init BUSY GPIO
    if (epaper_panel->busy_gpio_num >= 0)
    {
        // gpio_config_t io_conf = {
        //     .mode = GPIO_MODE_INPUT,
        //     .pull_down_en = 0x01,
        //     .pin_bit_mask = 1ULL << epaper_panel->busy_gpio_num,
        // };
        // io_conf.intr_type = GPIO_INTR_NEGEDGE;
        // ESP_LOGI(TAG, "Add handler for GPIO %d", epaper_panel->busy_gpio_num);
        // ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for BUSY line err");
        // ESP_GOTO_ON_ERROR(gpio_isr_handler_add(epaper_panel->busy_gpio_num, epaper_driver_gpio_isr_handler, epaper_panel), err, TAG, "configure GPIO for BUSY line err");

        // // Enable GPIO intr only before refreshing, to avoid other commands caused intr trigger
        // gpio_intr_disable(epaper_panel->busy_gpio_num);

        gpio_config_t io_conf = {
            .mode = GPIO_MODE_INPUT,
            .pull_down_en = 0x01,
            .pin_bit_mask = 1ULL << epaper_panel->busy_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "Configure GPIO for BUSY line err");
    }
    ESP_LOGD(TAG, "new epaper panel @%p", epaper_panel);
    return ret;
err:
    if (epaper_panel)
    {
        if (panel_dev_config->reset_gpio_num >= 0)
        {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        if (epaper_ssd1681_conf->busy_gpio_num >= 0)
        {
            gpio_reset_pin(epaper_ssd1681_conf->busy_gpio_num);
        }
        free(epaper_panel);
    }
    return ret;
}

static esp_err_t epaper_panel_del(esp_lcd_panel_t *panel)
{
    epaper_panel_t *epaper_panel = __containerof(panel, epaper_panel_t, base);
    // --- Reset used GPIO pins
    if ((epaper_panel->reset_gpio_num) >= 0)
    {
        gpio_reset_pin(epaper_panel->reset_gpio_num);
    }
    gpio_reset_pin(epaper_panel->busy_gpio_num);
    // --- Free allocated RAM
    if ((epaper_panel->_framebuffer) && (!(epaper_panel->_non_copy_mode)))
    {
        // Should not free if buffer is not allocated by driver
        free(epaper_panel->_framebuffer);
    }
    ESP_LOGD(TAG, "del ssd1681 epaper panel @%p", epaper_panel);
    free(epaper_panel);
    return ESP_OK;
}

static esp_err_t epaper_panel_reset(esp_lcd_panel_t *panel)
{
    epaper_panel_t *epaper_panel = __containerof(panel, epaper_panel_t, base);

    ESP_RETURN_ON_ERROR(gpio_set_level(epaper_panel->reset_gpio_num, 1), TAG, "gpio_set_level error");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(gpio_set_level(epaper_panel->reset_gpio_num, 0), TAG, "gpio_set_level error");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(epaper_panel->reset_gpio_num, 1), TAG, "gpio_set_level error");
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Wait for reset to complete");

    panel_epaper_wait_busy(panel);

    return ESP_OK;
}

esp_err_t epaper_panel_set_bitmap_color(esp_lcd_panel_t *panel, esp_lcd_ssd1681_bitmap_color_t color)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG, "panel handler is NULL");
    epaper_panel_t *epaper_panel = __containerof(panel, epaper_panel_t, base);
    epaper_panel->bitmap_color = color;
    return ESP_OK;
}

static esp_err_t epaper_panel_init(esp_lcd_panel_t *panel)
{
    epaper_panel_t *epaper_panel = __containerof(panel, epaper_panel_t, base);
    esp_lcd_panel_io_handle_t io = epaper_panel->io;

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SSD1681_CMD_SWRST, NULL, 0), TAG, "param SSD1681_CMD_SWRST err");
    panel_epaper_wait_busy(panel);

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SSD1681_CMD_DISP_UPDATE_CTRL, (uint8_t[]){0x40, 0x00}, 2), TAG, "param SSD1681_CMD_DISP_UPDATE_CTRL err");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(epaper_panel->io, SSD1681_CMD_SET_BORDER_WAVEFORM, (uint8_t[]){0x05}, 1), TAG, "SSD1681_CMD_SET_BORDER_WAVEFORM err");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(epaper_panel->io, 0x1A, (uint8_t[]){0x5A}, 1), TAG, "Temperature err");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(epaper_panel->io, 0x22, (uint8_t[]){0x91}, 1), TAG, "SSD1681_CMD_SET_BORDER_WAVEFORM err");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(epaper_panel->io, 0x20, NULL, 0), TAG, "SSD1681_CMD_SET_BORDER_WAVEFORM err");
    panel_epaper_wait_busy(panel);

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(epaper_panel->io, 0x11, (uint8_t[]){0x03}, 1), TAG, "SSD1681_CMD_SET_BORDER_WAVEFORM err");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(epaper_panel->io, 0x44, (uint8_t[]){(0 >> 3) & 0xFF, ((400 - 1) >> 3) & 0xFF}, 2), TAG, "SSD1681_CMD_SET_BORDER_WAVEFORM err");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(epaper_panel->io, 0x45, (uint8_t[]){0 & 0xFF, (0 >> 8) & 0xFF, (300 - 1) & 0xFF, ((300 - 1) >> 8) & 0xFF}, 4), TAG, "SSD1681_CMD_SET_BORDER_WAVEFORM err");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(epaper_panel->io, 0x4E, (uint8_t[]){0 & 0xFF}, 1), TAG, "SSD1681_CMD_SET_BORDER_WAVEFORM err");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(epaper_panel->io, 0x4F, (uint8_t[]){0 & 0xFF, (0 >> 8) & 0xFF}, 2), TAG, "SSD1681_CMD_SET_BORDER_WAVEFORM err");

    panel_epaper_wait_busy(panel);

    return ESP_OK;
}

static esp_err_t epaper_panel_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    ESP_LOGI(TAG, "epaper_panel_draw_bitmap: %d,%d %d,%d", x_start, y_start, x_end, y_end);

    epaper_panel_t *epaper_panel = __containerof(panel, epaper_panel_t, base);
   
    panel_epaper_wait_busy(panel);
   
    // if (gpio_get_level(epaper_panel->busy_gpio_num))
    // {
    //     ESP_LOGI(TAG, "Panel is busy. Cannot refresh");
    //     return ESP_ERR_NOT_FINISHED;
    // }

    // --- Calculate coordinates & sizes
    int len_x = abs(x_start - x_end);
    int len_y = abs(y_start - y_end);
    x_end--;
    y_end--;
    int buffer_size = len_x * len_y / 8;

    process_bitmap(panel, len_x, len_y, buffer_size, color_data);

    ESP_RETURN_ON_ERROR(panel_epaper_set_vram(epaper_panel->io, (uint8_t *)(epaper_panel->_framebuffer), NULL, (len_x * len_y / 8)), TAG, "panel_epaper_set_vram error");

    panel_epaper_wait_busy(panel);

    return ESP_OK;
}

static esp_err_t epaper_panel_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    // epaper_panel_t *epaper_panel = __containerof(panel, epaper_panel_t, base);
    // esp_lcd_panel_io_handle_t io = epaper_panel->io;

    // if (on_off)
    // {
    //     // Turn on display
    //     ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(epaper_panel->io, SSD1681_CMD_SET_DISP_UPDATE_CTRL, (uint8_t[]){SSD1681_PARAM_DISP_UPDATE_MODE_1}, 1), TAG, "SSD1681_CMD_SET_DISP_UPDATE_CTRL err");
    //     ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SSD1681_CMD_ACTIVE_DISP_UPDATE_SEQ, NULL, 0), TAG, "SSD1681_CMD_ACTIVE_DISP_UPDATE_SEQ err");
    //     panel_epaper_wait_busy(panel);
    // }
    // else
    // {
    //     // Sleep mode, BUSY pin will keep HIGH after entering sleep mode
    //     // Perform reset and re-run init to resume the display
    //     ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(epaper_panel->io, SSD1681_CMD_SLEEP_CTRL, (uint8_t[]){SSD1681_PARAM_SLEEP_MODE_1}, 1), TAG, "SSD1681_CMD_SLEEP_CTRL err");
    //     // BUSY pin will stay HIGH, so do not call panel_epaper_wait_busy() here
    // }

    return ESP_OK;
}

static esp_err_t process_bitmap(esp_lcd_panel_t *panel, int len_x, int len_y, int buffer_size, const void *color_data)
{
    ESP_LOGI(TAG, "process_bitmap: %d,%d %d", len_x, len_y, buffer_size);

    epaper_panel_t *epaper_panel = __containerof(panel, epaper_panel_t, base);

    memset(epaper_panel->_framebuffer, 0, len_x * len_y / 8);

    int black_pixel_count = 0;
    int white_pixel_count = 0;

    for (int i = 0; i < buffer_size * 8; i++)
    {
        uint8_t bitmap_byte = ((uint8_t *)(color_data))[8 + i / 8];

        uint8_t is_black_pixel = bitmap_byte & (0x01 << (7 - (i % 8)));

        if (is_black_pixel)
        {
            black_pixel_count++;
        }
        else
        {
            white_pixel_count++;
        }

        int index = i / 8;

        uint8_t current = (epaper_panel->_framebuffer)[index];

        if (is_black_pixel) // Set the bit to 1
        {
            (epaper_panel->_framebuffer)[index] = current & ~(0x80 >> (i % 8));
        }
        else
        {
            (epaper_panel->_framebuffer)[index] = current | (0x80 >> (i % 8));
        }
    }

    ESP_LOGI(TAG, "Rendered %d black pixels and %d white pixels (total: %d)", black_pixel_count, white_pixel_count, black_pixel_count + white_pixel_count);

    return ESP_OK;
}