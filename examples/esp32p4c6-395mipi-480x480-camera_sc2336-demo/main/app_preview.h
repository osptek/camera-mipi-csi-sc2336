#pragma once

#include "esp_err.h"
#include "app_display.h"

esp_err_t app_preview_init(const app_display_context_t *display_ctx);
esp_err_t app_preview_draw_frame(const uint8_t *frame_data, uint32_t width, uint32_t height, size_t len, void *user_data);
