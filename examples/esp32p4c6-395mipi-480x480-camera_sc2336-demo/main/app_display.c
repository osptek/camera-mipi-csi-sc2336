/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "app_display.h"

#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "hw_init.h"
#include "sdkconfig.h"

static const char *TAG = "app_display";

static esp_lv_adapter_rotation_t get_configured_rotation(void)
{
#if CONFIG_EXAMPLE_DISPLAY_ROTATION_0
    return ESP_LV_ADAPTER_ROTATE_0;
#elif CONFIG_EXAMPLE_DISPLAY_ROTATION_90
    return ESP_LV_ADAPTER_ROTATE_90;
#elif CONFIG_EXAMPLE_DISPLAY_ROTATION_180
    return ESP_LV_ADAPTER_ROTATE_180;
#elif CONFIG_EXAMPLE_DISPLAY_ROTATION_270
    return ESP_LV_ADAPTER_ROTATE_270;
#else
    return ESP_LV_ADAPTER_ROTATE_0;
#endif
}

static esp_lv_adapter_tear_avoid_mode_t get_tear_avoid_mode(void)
{
#if CONFIG_EXAMPLE_LCD_INTERFACE_MIPI_DSI
    ESP_LOGI(TAG, "Selected LCD interface: MIPI DSI");
    return ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_MIPI_DSI;
#elif CONFIG_EXAMPLE_LCD_INTERFACE_RGB
    ESP_LOGI(TAG, "Selected LCD interface: RGB");
    return ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;
#else
#if CONFIG_EXAMPLE_LCD_INTERFACE_QSPI
    ESP_LOGI(TAG, "Selected LCD interface: QSPI");
#elif CONFIG_EXAMPLE_LCD_INTERFACE_SPI_WITH_PSRAM
    ESP_LOGI(TAG, "Selected LCD interface: SPI (with PSRAM)");
#elif CONFIG_EXAMPLE_LCD_INTERFACE_SPI_WITHOUT_PSRAM
    ESP_LOGI(TAG, "Selected LCD interface: SPI (without PSRAM)");
#endif
    return ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT;
#endif
}

static lv_display_t *register_display(esp_lcd_panel_handle_t display_panel,
                                      esp_lcd_panel_io_handle_t display_io_handle,
                                      esp_lv_adapter_rotation_t rotation)
{
#if CONFIG_EXAMPLE_LCD_INTERFACE_MIPI_DSI
    esp_lv_adapter_display_config_t display_config = ESP_LV_ADAPTER_DISPLAY_MIPI_DEFAULT_CONFIG(
                                                         display_panel, display_io_handle, HW_LCD_H_RES, HW_LCD_V_RES, rotation);
#elif CONFIG_EXAMPLE_LCD_INTERFACE_RGB
    esp_lv_adapter_display_config_t display_config = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
                                                         display_panel, display_io_handle, HW_LCD_H_RES, HW_LCD_V_RES, rotation);
#elif CONFIG_EXAMPLE_LCD_INTERFACE_SPI_WITHOUT_PSRAM
    esp_lv_adapter_display_config_t display_config = ESP_LV_ADAPTER_DISPLAY_SPI_WITHOUT_PSRAM_DEFAULT_CONFIG(
                                                         display_panel, display_io_handle, HW_LCD_H_RES, HW_LCD_V_RES, rotation);
#else
    esp_lv_adapter_display_config_t display_config = ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(
                                                         display_panel, display_io_handle, HW_LCD_H_RES, HW_LCD_V_RES, rotation);
#endif

    return esp_lv_adapter_register_display(&display_config);
}

static esp_err_t init_input_device(lv_display_t *display, esp_lv_adapter_rotation_t rotation)
{
#if HW_USE_TOUCH
    ESP_LOGI(TAG, "Initializing touch panel");
    esp_lcd_touch_handle_t touch_handle = NULL;
    esp_err_t touch_err = hw_touch_init(&touch_handle, rotation);
    if (touch_err != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed (%s). Continuing without touch input.", esp_err_to_name(touch_err));
        return ESP_OK;
    }

    esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, touch_handle);
    if (esp_lv_adapter_register_touch(&touch_config) == NULL) {
        ESP_LOGE(TAG, "Failed to register touch");
        return ESP_FAIL;
    }

    return ESP_OK;
#elif HW_USE_ENCODER && CONFIG_ESP_LVGL_ADAPTER_ENABLE_KNOB
    ESP_LOGI(TAG, "Initializing encoder/knob");
    esp_lv_adapter_encoder_config_t encoder_config = {
        .disp = display,
        .encoder_a_b = hw_knob_get_config(),
        .encoder_enter = hw_knob_get_button(),
    };
    if (esp_lv_adapter_register_encoder(&encoder_config) == NULL) {
        ESP_LOGE(TAG, "Failed to register encoder");
        return ESP_FAIL;
    }

    return ESP_OK;
#else
    (void)display;
    (void)rotation;
    return ESP_OK;
#endif
}

esp_err_t app_display_init(app_display_context_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));

    esp_lcd_panel_handle_t display_panel = NULL;
    esp_lcd_panel_io_handle_t display_io_handle = NULL;
    esp_lv_adapter_rotation_t rotation = get_configured_rotation();
    esp_lv_adapter_tear_avoid_mode_t tear_avoid_mode = get_tear_avoid_mode();

    ESP_LOGI(TAG, "Initializing LCD: %dx%d", HW_LCD_H_RES, HW_LCD_V_RES);
    ESP_RETURN_ON_ERROR(hw_lcd_init(&display_panel, &display_io_handle, tear_avoid_mode, rotation), TAG, "LCD init failed");

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_config), TAG, "LVGL adapter init failed");

    lv_display_t *display = register_display(display_panel, display_io_handle, rotation);
    if (display == NULL) {
        ESP_LOGE(TAG, "Failed to register display");
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(init_input_device(display, rotation), TAG, "Input device init failed");
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "LVGL adapter start failed");

    ctx->display = display;
    ctx->panel = display_panel;
    ctx->io_handle = display_io_handle;
    ctx->rotation = rotation;

    ESP_LOGI(TAG, "Display and LVGL initialized");
    return ESP_OK;
}
