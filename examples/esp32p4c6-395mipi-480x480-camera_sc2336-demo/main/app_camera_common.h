#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

typedef struct app_camera_sensor_ops {
    const char *name;
    esp_err_t (*init_video_hw)(void);
    void (*apply_post_open_controls)(int fd);
} app_camera_sensor_ops_t;

const app_camera_sensor_ops_t *app_camera_get_sensor_ops(void);

const app_camera_sensor_ops_t *app_camera_get_sc2336_sensor_ops(void);

bool app_camera_sensor_is_selected(void);
const char *app_camera_get_device_path(void);

void app_camera_scan_i2c_bus(i2c_master_bus_handle_t bus_handle, int sda_pin, int scl_pin);
void app_camera_scan_i2c_with_temp_bus(int port, int sda_pin, int scl_pin);

esp_err_t app_camera_init_mipi_csi_hw(int reset_pin, int pwdn_pin);
esp_err_t app_camera_init_dvp_hw(void);
void app_camera_apply_common_controls(int fd);
