/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "app_preview.h"

#include <string.h>

#include "esp_cache.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "hw_init.h"

static const char *TAG = "app_preview";
static const uint32_t PREVIEW_BUFFER_COUNT = 3;

static app_display_context_t s_display_ctx;
static uint8_t *s_crop_buffers[3];
static size_t s_crop_buffer_size;
static uint32_t s_next_crop_buffer;
static bool s_logged_center_crop;
static bool s_logged_frame_info;
static uint32_t s_frame_counter;
static uint32_t s_last_frame_signature;
static bool s_logged_frame_signature;
static bool s_logged_byte_swap;
static bool s_logged_rb_swap;

static esp_err_t draw_preview_bitmap(const void *buffer, size_t size, uint32_t draw_width, uint32_t draw_height)
{
    ESP_RETURN_ON_ERROR(esp_cache_msync((void *)buffer, size,
                                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED),
                        TAG, "failed to sync preview buffer for lcd");

    return esp_lcd_panel_draw_bitmap(s_display_ctx.panel, 0, 0, draw_width, draw_height, buffer);
}

static uint32_t sample_frame_signature(const uint8_t *frame_data, size_t len)
{
    if (frame_data == NULL || len == 0) {
        return 0;
    }

    size_t step = len / 32;
    if (step == 0) {
        step = 1;
    }

    uint32_t signature = 0;
    for (size_t i = 0; i < len; i += step) {
        signature = (signature * 131U) + frame_data[i];
    }

    signature ^= frame_data[len - 1];
    return signature;
}

static void rgb565_swap_bytes_inplace(uint8_t *buffer, size_t size)
{
    for (size_t i = 0; i + 1 < size; i += 2) {
        uint8_t tmp = buffer[i];
        buffer[i] = buffer[i + 1];
        buffer[i + 1] = tmp;
    }
}

static void rgb565_swap_rb_inplace(uint8_t *buffer, size_t size)
{
    uint16_t *pixels = (uint16_t *)buffer;
    size_t pixel_count = size / sizeof(uint16_t);

    for (size_t i = 0; i < pixel_count; i++) {
        uint16_t pixel = pixels[i];
        uint16_t r = (pixel >> 11) & 0x1F;
        uint16_t g = (pixel >> 5) & 0x3F;
        uint16_t b = pixel & 0x1F;
        pixels[i] = (uint16_t)((b << 11) | (g << 5) | r);
    }
}

static esp_err_t ensure_crop_buffers(size_t size)
{
    if (s_crop_buffer_size >= size && s_crop_buffers[0] != NULL) {
        return ESP_OK;
    }

    for (uint32_t i = 0; i < PREVIEW_BUFFER_COUNT; i++) {
        void *new_buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ESP_RETURN_ON_FALSE(new_buffer != NULL, ESP_ERR_NO_MEM, TAG, "failed to allocate crop buffer %" PRIu32, i);

        if (s_crop_buffers[i] != NULL) {
            heap_caps_free(s_crop_buffers[i]);
        }

        s_crop_buffers[i] = new_buffer;
    }

    s_crop_buffer_size = size;
    s_next_crop_buffer = 0;
    return ESP_OK;
}

esp_err_t app_preview_init(const app_display_context_t *display_ctx)
{
    ESP_RETURN_ON_FALSE(display_ctx != NULL, ESP_ERR_INVALID_ARG, TAG, "display ctx is null");
    s_display_ctx = *display_ctx;
    return ESP_OK;
}

esp_err_t app_preview_draw_frame(const uint8_t *frame_data, uint32_t width, uint32_t height, size_t len, void *user_data)
{
    uint32_t draw_width = width > HW_LCD_H_RES ? HW_LCD_H_RES : width;
    uint32_t draw_height = height > HW_LCD_V_RES ? HW_LCD_V_RES : height;
    uint32_t bytes_per_pixel;

    (void)len;
    (void)user_data;

    ESP_RETURN_ON_FALSE(frame_data != NULL, ESP_ERR_INVALID_ARG, TAG, "frame data is null");
    ESP_RETURN_ON_FALSE(s_display_ctx.panel != NULL, ESP_ERR_INVALID_STATE, TAG, "display panel is null");
    ESP_RETURN_ON_FALSE(width > 0 && height > 0, ESP_ERR_INVALID_ARG, TAG, "invalid frame size");

    bytes_per_pixel = len / (width * height);
    ESP_RETURN_ON_FALSE(bytes_per_pixel > 0, ESP_ERR_INVALID_ARG, TAG, "invalid frame stride");

    if (!s_logged_frame_info) {
        ESP_LOGI(TAG, "Preview frame info: %lux%lu len=%u bytes_per_pixel=%lu",
                 width, height, (unsigned)len, bytes_per_pixel);
        s_logged_frame_info = true;
    }

    s_frame_counter++;
    if ((s_frame_counter % 30U) == 1U) {
        uint32_t frame_signature = sample_frame_signature(frame_data, len);
        if (!s_logged_frame_signature || frame_signature != s_last_frame_signature) {
            ESP_LOGI(TAG, "Preview frame signature[%lu]=0x%08" PRIx32,
                     s_frame_counter, frame_signature);
            s_last_frame_signature = frame_signature;
            s_logged_frame_signature = true;
        }
    }

    if (width == draw_width && height == draw_height) {
        ESP_RETURN_ON_ERROR(ensure_crop_buffers(len), TAG, "failed to prepare direct preview buffers");
        uint8_t *draw_buffer = s_crop_buffers[s_next_crop_buffer];
        s_next_crop_buffer = (s_next_crop_buffer + 1) % PREVIEW_BUFFER_COUNT;
        memcpy(draw_buffer, frame_data, len);
#if CONFIG_APP_CAMERA_RGB565_BYTE_SWAP
        if (bytes_per_pixel == 2) {
            rgb565_swap_bytes_inplace(draw_buffer, len);
            if (!s_logged_byte_swap) {
                ESP_LOGI(TAG, "Applying RGB565 byte swap for preview");
                s_logged_byte_swap = true;
            }
        }
#endif
#if CONFIG_APP_CAMERA_RGB565_RB_SWAP
        if (bytes_per_pixel == 2) {
            rgb565_swap_rb_inplace(draw_buffer, len);
            if (!s_logged_rb_swap) {
                ESP_LOGI(TAG, "Applying RGB565 R/B swap for preview");
                s_logged_rb_swap = true;
            }
        }
#endif
        return draw_preview_bitmap(draw_buffer, len, draw_width, draw_height);
    }

    uint32_t crop_x = (width - draw_width) / 2;
    uint32_t crop_y = (height - draw_height) / 2;
    size_t row_size = draw_width * bytes_per_pixel;
    size_t crop_size = row_size * draw_height;

    ESP_RETURN_ON_ERROR(ensure_crop_buffers(crop_size), TAG, "failed to prepare crop buffers");

    uint8_t *crop_buffer = s_crop_buffers[s_next_crop_buffer];
    s_next_crop_buffer = (s_next_crop_buffer + 1) % PREVIEW_BUFFER_COUNT;

    const uint8_t *src = frame_data + ((crop_y * width) + crop_x) * bytes_per_pixel;
    uint8_t *dst = crop_buffer;

    for (uint32_t row = 0; row < draw_height; row++) {
        memcpy(dst, src, row_size);
        dst += row_size;
        src += width * bytes_per_pixel;
    }

#if CONFIG_APP_CAMERA_RGB565_BYTE_SWAP
    if (bytes_per_pixel == 2) {
        rgb565_swap_bytes_inplace(crop_buffer, crop_size);
        if (!s_logged_byte_swap) {
            ESP_LOGI(TAG, "Applying RGB565 byte swap for preview");
            s_logged_byte_swap = true;
        }
    }
#endif
#if CONFIG_APP_CAMERA_RGB565_RB_SWAP
    if (bytes_per_pixel == 2) {
        rgb565_swap_rb_inplace(crop_buffer, crop_size);
        if (!s_logged_rb_swap) {
            ESP_LOGI(TAG, "Applying RGB565 R/B swap for preview");
            s_logged_rb_swap = true;
        }
    }
#endif

    if (!s_logged_center_crop) {
        ESP_LOGI(TAG, "Center cropping camera frame %lux%lu to LCD %dx%d",
                 width, height, HW_LCD_H_RES, HW_LCD_V_RES);
        s_logged_center_crop = true;
    }

    return draw_preview_bitmap(crop_buffer, crop_size, draw_width, draw_height);
}
