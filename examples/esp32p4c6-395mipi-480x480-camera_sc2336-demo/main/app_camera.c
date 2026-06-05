/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "app_camera.h"
#include "app_camera_common.h"

#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "esp_cache.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_private/esp_cache_private.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linux/videodev2.h"
#include "sdkconfig.h"

#if CONFIG_APP_CAMERA_PIXEL_FORMAT_RGB888
#define APP_CAMERA_V4L2_PIXEL_FORMAT    V4L2_PIX_FMT_RGB24
#else
#define APP_CAMERA_V4L2_PIXEL_FORMAT    V4L2_PIX_FMT_RGB565
#endif

typedef struct app_camera_context {
    int fd;
    uint8_t *buffers[CONFIG_APP_CAMERA_BUFFER_COUNT];
    size_t buffer_lengths[CONFIG_APP_CAMERA_BUFFER_COUNT];
    TaskHandle_t task_handle;
    uint32_t width;
    uint32_t height;
    uint32_t buffer_type;
    uint32_t memory_type;
    struct v4l2_buffer v4l2_buf;
    app_camera_frame_cb_t frame_cb;
    void *frame_cb_user_data;
} app_camera_context_t;

static const char *TAG = "app_camera";
static struct app_camera_context s_camera_ctx;

static size_t camera_buffer_alignment(void)
{
    size_t alignment = 0;

    if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &alignment) != ESP_OK || alignment == 0) {
        alignment = 64;
    }

    return alignment;
}

static esp_err_t camera_invalidate_dma_buffer_for_cpu(void *buffer, size_t size)
{
    size_t alignment = camera_buffer_alignment();
    size_t sync_size = (size + alignment - 1) / alignment * alignment;

    return esp_cache_msync(buffer, sync_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}

static esp_err_t open_and_configure_camera(app_camera_context_t *ctx)
{
    const app_camera_sensor_ops_t *sensor_ops = app_camera_get_sensor_ops();
    const char *device_path = app_camera_get_device_path();
    struct v4l2_format format = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    };
    struct v4l2_requestbuffers req = {
        .count = CONFIG_APP_CAMERA_BUFFER_COUNT,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_USERPTR,
    };

    ESP_RETURN_ON_FALSE(sensor_ops != NULL, ESP_ERR_INVALID_STATE, TAG, "camera sensor ops are not available");

    ctx->fd = open(device_path, O_RDONLY);
    if (ctx->fd < 0) {
        ESP_LOGE(TAG, "Failed to open camera device %s", device_path);
        return ESP_FAIL;
    }

    if (ioctl(ctx->fd, VIDIOC_G_FMT, &format) != 0) {
        ESP_LOGE(TAG, "Failed to get current camera format");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera default format: %lux%lu, fourcc=0x%08" PRIx32 ", device=%s",
             format.fmt.pix.width, format.fmt.pix.height, format.fmt.pix.pixelformat, device_path);

    if (CONFIG_APP_CAMERA_FRAME_WIDTH != format.fmt.pix.width || CONFIG_APP_CAMERA_FRAME_HEIGHT != format.fmt.pix.height) {
        ESP_LOGW(TAG, "Ignoring configured preview size %dx%d, capture device keeps sensor default %lux%lu",
                 CONFIG_APP_CAMERA_FRAME_WIDTH, CONFIG_APP_CAMERA_FRAME_HEIGHT,
                 format.fmt.pix.width, format.fmt.pix.height);
    }

    uint32_t current_pixelformat = format.fmt.pix.pixelformat;
    if (current_pixelformat != APP_CAMERA_V4L2_PIXEL_FORMAT) {
        format.fmt.pix.pixelformat = APP_CAMERA_V4L2_PIXEL_FORMAT;
        if (ioctl(ctx->fd, VIDIOC_S_FMT, &format) != 0) {
            if (current_pixelformat == V4L2_PIX_FMT_RGB565 || current_pixelformat == V4L2_PIX_FMT_RGB24) {
                ESP_LOGW(TAG, "Requested pixel format 0x%08" PRIx32 " is not supported, fallback to current format 0x%08" PRIx32,
                         APP_CAMERA_V4L2_PIXEL_FORMAT, current_pixelformat);
                memset(&format, 0, sizeof(format));
                format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (ioctl(ctx->fd, VIDIOC_G_FMT, &format) != 0) {
                    ESP_LOGE(TAG, "Failed to re-read camera format after fallback");
                    return ESP_FAIL;
                }
            } else {
                ESP_LOGE(TAG, "Failed to set camera format");
                return ESP_FAIL;
            }
        }
    }

    if (ioctl(ctx->fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "Failed to request camera buffers");
        return ESP_FAIL;
    }

    ctx->width = format.fmt.pix.width;
    ctx->height = format.fmt.pix.height;
    ctx->buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ctx->memory_type = req.memory;
    sensor_ops->apply_post_open_controls(ctx->fd);

    for (int i = 0; i < CONFIG_APP_CAMERA_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf = {
            .type = req.type,
            .memory = req.memory,
            .index = i,
        };

        if (ioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "Failed to query camera buffer %d", i);
            return ESP_FAIL;
        }

        ctx->buffer_lengths[i] = buf.length;
        ctx->buffers[i] = heap_caps_aligned_calloc(camera_buffer_alignment(), 1, buf.length,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_CACHE_ALIGNED);
        if (ctx->buffers[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate camera buffer %d, len=%u", i, (unsigned)buf.length);
            return ESP_FAIL;
        }

        buf.m.userptr = (unsigned long)ctx->buffers[i];
        buf.length = ctx->buffer_lengths[i];
        if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "Failed to queue camera buffer %d", i);
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Camera format ready: %lux%lu via %s", ctx->width, ctx->height, device_path);
    return ESP_OK;
}

static esp_err_t app_camera_stream_on(app_camera_context_t *ctx)
{
    int type = (int)ctx->buffer_type;

    ESP_LOGI(TAG, "Starting stream: type=%d memory=%" PRIu32 " size=%lux%lu",
             type, ctx->memory_type, ctx->width, ctx->height);

    if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "Failed to start camera stream: errno=%d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void app_camera_task(void *arg)
{
    app_camera_context_t *ctx = (app_camera_context_t *)arg;
    int type = (int)ctx->buffer_type;

    while (1) {
        memset(&ctx->v4l2_buf, 0, sizeof(ctx->v4l2_buf));
        ctx->v4l2_buf.type = ctx->buffer_type;
        ctx->v4l2_buf.memory = ctx->memory_type;

        if (ioctl(ctx->fd, VIDIOC_DQBUF, &ctx->v4l2_buf) != 0) {
            ESP_LOGE(TAG, "Failed to dequeue camera buffer");
            break;
        }

        if ((ctx->v4l2_buf.flags & V4L2_BUF_FLAG_DONE) && ctx->v4l2_buf.bytesused > 0) {
            if (ctx->frame_cb != NULL) {
                uint32_t index = ctx->v4l2_buf.index;
                esp_err_t cache_ret = camera_invalidate_dma_buffer_for_cpu(ctx->buffers[index],
                                                                           ctx->v4l2_buf.bytesused);
                if (cache_ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to invalidate camera buffer cache: %s", esp_err_to_name(cache_ret));
                }
                esp_err_t ret = ctx->frame_cb(ctx->buffers[index], ctx->width, ctx->height,
                                              ctx->v4l2_buf.bytesused, ctx->frame_cb_user_data);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Frame callback failed: %s", esp_err_to_name(ret));
                }
            }
        }

        ctx->v4l2_buf.m.userptr = (unsigned long)ctx->buffers[ctx->v4l2_buf.index];
        ctx->v4l2_buf.length = ctx->buffer_lengths[ctx->v4l2_buf.index];
        if (ioctl(ctx->fd, VIDIOC_QBUF, &ctx->v4l2_buf) != 0) {
            ESP_LOGE(TAG, "Failed to requeue camera buffer");
            break;
        }
    }

    ioctl(ctx->fd, VIDIOC_STREAMOFF, &type);
    vTaskDelete(NULL);
}

esp_err_t app_camera_start(app_camera_frame_cb_t frame_cb, void *user_data)
{
    app_camera_context_t *ctx = &s_camera_ctx;
    const app_camera_sensor_ops_t *sensor_ops = app_camera_get_sensor_ops();

    ESP_RETURN_ON_FALSE(frame_cb != NULL, ESP_ERR_INVALID_ARG, TAG, "frame callback is null");
    ESP_RETURN_ON_FALSE(app_camera_sensor_is_selected(), ESP_ERR_NOT_SUPPORTED, TAG, "no camera sensor is enabled");
    ESP_RETURN_ON_FALSE(sensor_ops != NULL, ESP_ERR_NOT_SUPPORTED, TAG, "no camera backend selected");

    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = -1;
    ctx->frame_cb = frame_cb;
    ctx->frame_cb_user_data = user_data;

    ESP_LOGI(TAG, "Initializing camera backend: %s", sensor_ops->name);
    ESP_RETURN_ON_ERROR(sensor_ops->init_video_hw(), TAG, "Video hardware init failed");
    ESP_RETURN_ON_ERROR(open_and_configure_camera(ctx), TAG, "Camera open/config failed");
    ESP_RETURN_ON_ERROR(app_camera_stream_on(ctx), TAG, "Camera stream start failed");

    BaseType_t task_ret = xTaskCreate(app_camera_task, "camera_preview", CONFIG_APP_CAMERA_TASK_STACK_SIZE,
                                      ctx, CONFIG_APP_CAMERA_TASK_PRIORITY, &ctx->task_handle);
    ESP_RETURN_ON_FALSE(task_ret == pdPASS, ESP_FAIL, TAG, "Failed to create camera preview task");

    ESP_LOGI(TAG, "Camera preview started");
    return ESP_OK;
}
