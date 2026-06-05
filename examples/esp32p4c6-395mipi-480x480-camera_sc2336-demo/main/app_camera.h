#pragma once

#include "esp_err.h"
#include "app_display.h"

typedef esp_err_t (*app_camera_frame_cb_t)(const uint8_t *frame_data, uint32_t width, uint32_t height, size_t len, void *user_data);

esp_err_t app_camera_start(app_camera_frame_cb_t frame_cb, void *user_data);
