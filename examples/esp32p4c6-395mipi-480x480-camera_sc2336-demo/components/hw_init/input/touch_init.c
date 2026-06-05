/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "driver/i2c_master.h"
#include <stdbool.h>
#include "esp_log.h"
#include "hw_init.h"
#include "input/touch_rotation_helper.h"

#if HW_USE_TOUCH

#if CONFIG_EXAMPLE_LCD_INTERFACE_RGB
#include "esp_lcd_touch_gt1151.h"
#define HW_LCD_TOUCH_RST                (GPIO_NUM_NC)
#define HW_LCD_TOUCH_INT                (GPIO_NUM_NC)
#define HW_I2C_SDA                      (GPIO_NUM_47)
#define HW_I2C_SCL                      (GPIO_NUM_48)
#define TOUCH_CONTROLLER_NAME           "GT1151"
#define TOUCH_I2C_EXPECTED_ADDR         ESP_LCD_TOUCH_IO_I2C_GT1151_ADDRESS
#define TOUCH_ROTATION_TYPE             TOUCH_ROTATION_STANDARD
#define TOUCH_CONTROLLER_TYPE_GT1151    1

#elif CONFIG_EXAMPLE_LCD_INTERFACE_MIPI_DSI
#include "esp_lcd_touch_st7123.h"
#define HW_LCD_TOUCH_RST                (GPIO_NUM_NC)
#define HW_LCD_TOUCH_INT                (GPIO_NUM_NC)
#define HW_I2C_SDA                      (GPIO_NUM_7)
#define HW_I2C_SCL                      (GPIO_NUM_8)
#define TOUCH_CONTROLLER_NAME           "ST7123"
#define TOUCH_I2C_EXPECTED_ADDR         ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS
#define TOUCH_ROTATION_TYPE             TOUCH_ROTATION_STANDARD
#define TOUCH_CONTROLLER_TYPE_ST7123     1

#elif CONFIG_EXAMPLE_LCD_INTERFACE_QSPI
#include "esp_lcd_touch_cst816s.h"
#define HW_LCD_TOUCH_RST                (GPIO_NUM_NC)
#define HW_LCD_TOUCH_INT                (GPIO_NUM_10)
#define HW_I2C_SDA                      (GPIO_NUM_2)
#define HW_I2C_SCL                      (GPIO_NUM_1)
#define TOUCH_CONTROLLER_NAME           "CST816S"
#define TOUCH_I2C_EXPECTED_ADDR         ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS
#define TOUCH_ROTATION_TYPE             TOUCH_ROTATION_STANDARD
#define TOUCH_CONTROLLER_TYPE_CST816S   1

#elif CONFIG_EXAMPLE_LCD_INTERFACE_SPI_WITH_PSRAM
#include "esp_lcd_touch_gt911.h"
#define HW_LCD_TOUCH_RST                (GPIO_NUM_NC)
#define HW_LCD_TOUCH_INT                (GPIO_NUM_3)
#define HW_I2C_SDA                      (GPIO_NUM_8)
#define HW_I2C_SCL                      (GPIO_NUM_18)
#define TOUCH_CONTROLLER_NAME           "GT911"
#define TOUCH_I2C_EXPECTED_ADDR         ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS
#define TOUCH_ROTATION_TYPE             TOUCH_ROTATION_STANDARD
#define TOUCH_CONTROLLER_TYPE_GT911     1

#else
#include "esp_lcd_touch_gt911.h"
#define HW_LCD_TOUCH_RST                (GPIO_NUM_NC)
#define HW_LCD_TOUCH_INT                (GPIO_NUM_NC)
#define HW_I2C_SDA                      (GPIO_NUM_7)
#define HW_I2C_SCL                      (GPIO_NUM_8)
#define TOUCH_CONTROLLER_NAME           "GT911"
#define TOUCH_I2C_EXPECTED_ADDR         ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS
#define TOUCH_ROTATION_TYPE             TOUCH_ROTATION_STANDARD
#define TOUCH_CONTROLLER_TYPE_GT911     1
#endif

#define HW_I2C_NUM                          (I2C_NUM_0)
#define HW_I2C_CLK_SPEED_HZ                 (400000)

static const char *TAG = "hw_touch_init";
static i2c_master_bus_handle_t s_touch_i2c_bus = NULL;

static void hw_touch_i2c_scan(void)
{
    bool found = false;

    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
        if (i2c_master_probe(s_touch_i2c_bus, addr, 20) == ESP_OK) {
            ESP_LOGI(TAG, "I2C device detected at 0x%02X", addr);
            found = true;
        }
    }

    if (!found) {
        ESP_LOGW(TAG, "No I2C devices detected on touch bus (SDA=%d, SCL=%d)", HW_I2C_SDA, HW_I2C_SCL);
    }
}

static esp_err_t hw_touch_i2c_init(void)
{
    if (s_touch_i2c_bus) {
        return ESP_OK;
    }

    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = HW_I2C_SDA,
        .scl_io_num = HW_I2C_SCL,
        .i2c_port = HW_I2C_NUM,
    };
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_conf, &s_touch_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C touch bus on SDA=%d SCL=%d: %s",
                 HW_I2C_SDA, HW_I2C_SCL, esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t hw_touch_init(esp_lcd_touch_handle_t *ret_touch, esp_lv_adapter_rotation_t rotation)
{
    ESP_LOGI(TAG, "Initializing touch controller (%s)", TOUCH_CONTROLLER_NAME);
    ESP_LOGI(TAG, "Touch I2C config: port=%d SDA=%d SCL=%d speed=%d expected_addr=0x%02X",
             HW_I2C_NUM, HW_I2C_SDA, HW_I2C_SCL, HW_I2C_CLK_SPEED_HZ, TOUCH_I2C_EXPECTED_ADDR);

    esp_err_t ret = hw_touch_i2c_init();
    if (ret != ESP_OK) {
        return ret;
    }

    if (i2c_master_probe(s_touch_i2c_bus, TOUCH_I2C_EXPECTED_ADDR, 20) != ESP_OK) {
        ESP_LOGW(TAG, "Touch controller did not ACK at 0x%02X before init", TOUCH_I2C_EXPECTED_ADDR);
        hw_touch_i2c_scan();
    }

    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;
    touch_get_rotation_flags(TOUCH_ROTATION_TYPE, rotation, &swap_xy, &mirror_x, &mirror_y);

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = HW_LCD_H_RES,
        .y_max = HW_LCD_V_RES,
        .rst_gpio_num = HW_LCD_TOUCH_RST,
        .int_gpio_num = HW_LCD_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
    };

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;

    /* Initialize touch controller based on type */
#if defined(TOUCH_CONTROLLER_TYPE_GT1151)
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT1151_CONFIG();
    tp_io_config.scl_speed_hz = HW_I2C_CLK_SPEED_HZ;
    ret = esp_lcd_new_panel_io_i2c(s_touch_i2c_bus, &tp_io_config, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GT1151 panel IO: %s", esp_err_to_name(ret));
        return ret;
    }
    return esp_lcd_touch_new_i2c_gt1151(tp_io_handle, &tp_cfg, ret_touch);

#elif defined(TOUCH_CONTROLLER_TYPE_CST816S)
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    tp_io_config.scl_speed_hz = HW_I2C_CLK_SPEED_HZ;
    ret = esp_lcd_new_panel_io_i2c(s_touch_i2c_bus, &tp_io_config, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create CST816S panel IO: %s", esp_err_to_name(ret));
        return ret;
    }
    return esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, ret_touch);

#elif defined(TOUCH_CONTROLLER_TYPE_GT911)
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = HW_I2C_CLK_SPEED_HZ;
    ret = esp_lcd_new_panel_io_i2c(s_touch_i2c_bus, &tp_io_config, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GT911 panel IO: %s", esp_err_to_name(ret));
        return ret;
    }
    return esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, ret_touch);

#elif defined(TOUCH_CONTROLLER_TYPE_ST7123)
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG();
    tp_io_config.scl_speed_hz = HW_I2C_CLK_SPEED_HZ;
    ret = esp_lcd_new_panel_io_i2c(s_touch_i2c_bus, &tp_io_config, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ST7123 panel IO: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_lcd_touch_new_i2c_st7123(tp_io_handle, &tp_cfg, ret_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "ST7123 init failed (%s). Verify pull-ups and try swapping touch pins SDA/SCL (current SDA=%d SCL=%d)",
                 esp_err_to_name(ret), HW_I2C_SDA, HW_I2C_SCL);
    }
    return ret;

#else
#error "No touch controller type defined"
#endif
}

i2c_master_bus_handle_t hw_touch_get_i2c_bus(void)
{
    return s_touch_i2c_bus;
}

#endif
