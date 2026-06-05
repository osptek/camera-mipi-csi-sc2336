/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "app_camera_common.h"

#include <string.h>
#include <sys/ioctl.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "hw_init.h"
#include "linux/videodev2.h"
#include "sdkconfig.h"

static const char *TAG = "app_camera_common";

const app_camera_sensor_ops_t *app_camera_get_sensor_ops(void)
{
    return app_camera_get_sc2336_sensor_ops();
}

bool app_camera_sensor_is_selected(void)
{
    return CONFIG_CAMERA_SC2336;
}

const char *app_camera_get_device_path(void)
{
#if CONFIG_APP_CAMERA_INTERFACE_MIPI_CSI
    return ESP_VIDEO_MIPI_CSI_DEVICE_NAME;
#elif CONFIG_APP_CAMERA_INTERFACE_DVP
    return ESP_VIDEO_DVP_DEVICE_NAME;
#else
    return NULL;
#endif
}

void app_camera_scan_i2c_bus(i2c_master_bus_handle_t bus_handle, int sda_pin, int scl_pin)
{
    bool found = false;

    ESP_LOGI(TAG, "Scanning camera I2C bus on SDA=%d SCL=%d", sda_pin, scl_pin);
    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
        if (i2c_master_probe(bus_handle, addr, 20) == ESP_OK) {
            ESP_LOGI(TAG, "Camera I2C device detected at 0x%02X", addr);
            found = true;
        }
    }

    if (!found) {
        ESP_LOGW(TAG, "No I2C devices detected on camera bus (SDA=%d SCL=%d)", sda_pin, scl_pin);
    }
}

void app_camera_scan_i2c_with_temp_bus(int port, int sda_pin, int scl_pin)
{
    i2c_master_bus_handle_t bus_handle = NULL;
    const i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = port,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create temporary camera I2C bus on SDA=%d SCL=%d: %s",
                 sda_pin, scl_pin, esp_err_to_name(ret));
        return;
    }

    app_camera_scan_i2c_bus(bus_handle, sda_pin, scl_pin);
    ESP_ERROR_CHECK(i2c_del_master_bus(bus_handle));
}

esp_err_t app_camera_init_mipi_csi_hw(int reset_pin, int pwdn_pin)
{
#if CONFIG_APP_CAMERA_INTERFACE_MIPI_CSI
    i2c_master_bus_handle_t shared_i2c_bus = NULL;
#if HW_USE_TOUCH
    shared_i2c_bus = hw_touch_get_i2c_bus();
#endif

    if (shared_i2c_bus != NULL) {
        app_camera_scan_i2c_bus(shared_i2c_bus,
                                CONFIG_APP_CAMERA_MIPI_SCCB_SDA_PIN,
                                CONFIG_APP_CAMERA_MIPI_SCCB_SCL_PIN);
    } else {
        app_camera_scan_i2c_with_temp_bus(CONFIG_APP_CAMERA_SCCB_I2C_PORT,
                                          CONFIG_APP_CAMERA_MIPI_SCCB_SDA_PIN,
                                          CONFIG_APP_CAMERA_MIPI_SCCB_SCL_PIN);
    }

    esp_video_init_csi_config_t csi_config = {
        .sccb_config = {
            .freq = CONFIG_APP_CAMERA_SCCB_I2C_FREQ,
        },
        .reset_pin = reset_pin,
        .pwdn_pin = pwdn_pin,
    };
    if (shared_i2c_bus != NULL) {
        csi_config.sccb_config.init_sccb = false;
        csi_config.sccb_config.i2c_handle = shared_i2c_bus;
    } else {
        csi_config.sccb_config.init_sccb = true;
        csi_config.sccb_config.i2c_config.port = CONFIG_APP_CAMERA_SCCB_I2C_PORT;
        csi_config.sccb_config.i2c_config.scl_pin = CONFIG_APP_CAMERA_MIPI_SCCB_SCL_PIN;
        csi_config.sccb_config.i2c_config.sda_pin = CONFIG_APP_CAMERA_MIPI_SCCB_SDA_PIN;
    }
#if CONFIG_APP_CAMERA_MIPI_DONT_INIT_LDO
    csi_config.dont_init_ldo = true;
#endif
    const esp_video_init_config_t video_config = {
        .csi = &csi_config,
    };

    return esp_video_init(&video_config);
#else
    (void)reset_pin;
    (void)pwdn_pin;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t app_camera_init_dvp_hw(void)
{
#if CONFIG_APP_CAMERA_INTERFACE_DVP
    app_camera_scan_i2c_with_temp_bus(CONFIG_APP_CAMERA_SCCB_I2C_PORT,
                                      CONFIG_APP_CAMERA_DVP_SCCB_SDA_PIN,
                                      CONFIG_APP_CAMERA_DVP_SCCB_SCL_PIN);

    esp_video_init_dvp_config_t dvp_config = {
        .sccb_config = {
            .init_sccb = true,
            .i2c_config = {
                .port = CONFIG_APP_CAMERA_SCCB_I2C_PORT,
                .scl_pin = CONFIG_APP_CAMERA_DVP_SCCB_SCL_PIN,
                .sda_pin = CONFIG_APP_CAMERA_DVP_SCCB_SDA_PIN,
            },
            .freq = CONFIG_APP_CAMERA_SCCB_I2C_FREQ,
        },
        .reset_pin = CONFIG_APP_CAMERA_DVP_RESET_PIN,
        .pwdn_pin = CONFIG_APP_CAMERA_DVP_PWDN_PIN,
        .dvp_pin = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {
                CONFIG_APP_CAMERA_DVP_D0_PIN, CONFIG_APP_CAMERA_DVP_D1_PIN,
                CONFIG_APP_CAMERA_DVP_D2_PIN, CONFIG_APP_CAMERA_DVP_D3_PIN,
                CONFIG_APP_CAMERA_DVP_D4_PIN, CONFIG_APP_CAMERA_DVP_D5_PIN,
                CONFIG_APP_CAMERA_DVP_D6_PIN, CONFIG_APP_CAMERA_DVP_D7_PIN,
            },
            .vsync_io = CONFIG_APP_CAMERA_DVP_VSYNC_PIN,
            .de_io = CONFIG_APP_CAMERA_DVP_DE_PIN,
            .pclk_io = CONFIG_APP_CAMERA_DVP_PCLK_PIN,
            .xclk_io = CONFIG_APP_CAMERA_DVP_XCLK_PIN,
        },
        .xclk_freq = CONFIG_APP_CAMERA_DVP_XCLK_FREQ,
    };
    const esp_video_init_config_t video_config = {
        .dvp = &dvp_config,
    };

    return esp_video_init(&video_config);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void apply_camera_flip_controls(int fd)
{
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control control[1];

#if CONFIG_APP_CAMERA_ENABLE_VFLIP
    memset(&controls, 0, sizeof(controls));
    memset(control, 0, sizeof(control));
    controls.ctrl_class = V4L2_CTRL_CLASS_USER;
    controls.count = 1;
    controls.controls = control;
    control[0].id = V4L2_CID_VFLIP;
    control[0].value = 1;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
        ESP_LOGW(TAG, "Failed to enable camera VFLIP");
    }
#endif

#if CONFIG_APP_CAMERA_ENABLE_HFLIP
    memset(&controls, 0, sizeof(controls));
    memset(control, 0, sizeof(control));
    controls.ctrl_class = V4L2_CTRL_CLASS_USER;
    controls.count = 1;
    controls.controls = control;
    control[0].id = V4L2_CID_HFLIP;
    control[0].value = 1;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
        ESP_LOGW(TAG, "Failed to enable camera HFLIP");
    }
#endif
}

static void apply_camera_test_pattern_control(int fd)
{
#if CONFIG_APP_CAMERA_ENABLE_TEST_PATTERN
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control control[1];

    memset(&controls, 0, sizeof(controls));
    memset(control, 0, sizeof(control));
    controls.ctrl_class = V4L2_CTRL_CLASS_IMAGE_PROC;
    controls.count = 1;
    controls.controls = control;
    control[0].id = V4L2_CID_TEST_PATTERN;
    control[0].value = 1;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
        ESP_LOGW(TAG, "Failed to enable camera test pattern");
    } else {
        ESP_LOGI(TAG, "Camera test pattern enabled");
    }
#else
    (void)fd;
#endif
}

void app_camera_apply_common_controls(int fd)
{
    apply_camera_flip_controls(fd);
    apply_camera_test_pattern_control(fd);
}
