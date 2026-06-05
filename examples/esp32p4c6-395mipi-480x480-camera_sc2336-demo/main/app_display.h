#pragma once

#include "esp_err.h"
#include "esp_lv_adapter.h"

typedef struct {
    lv_display_t *display;
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io_handle;
    esp_lv_adapter_rotation_t rotation;
} app_display_context_t;

esp_err_t app_display_init(app_display_context_t *ctx);
