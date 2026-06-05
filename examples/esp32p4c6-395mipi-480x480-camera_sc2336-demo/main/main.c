/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file main.c
 * @brief Unified LVGL demo supporting multiple LCD interface types
 *
 * This example demonstrates how to use LVGL with different LCD interfaces
 * (MIPI DSI, QSPI, RGB, SPI) in a single unified codebase.
 * The interface type and hardware configuration can be selected via menuconfig.
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_level.h"
#include "app_camera.h"
#include "app_display.h"
#include "app_preview.h"

static const char *TAG = "main";

void app_main()
{
    app_display_context_t display_ctx;
    esp_err_t ret;

    esp_log_level_set("esp_video", ESP_LOG_DEBUG);
    esp_log_level_set("csi_video", ESP_LOG_DEBUG);
    esp_log_level_set("isp_video", ESP_LOG_DEBUG);
    esp_log_level_set("ISP", ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "Starting camera demo");
    ESP_ERROR_CHECK(app_display_init(&display_ctx));
    ESP_ERROR_CHECK(app_preview_init(&display_ctx));

    ret = app_camera_start(app_preview_draw_frame, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera start failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Enable a concrete camera sensor in menuconfig before running preview");
        return;
    }
}
