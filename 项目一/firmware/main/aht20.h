/* AHT20 温湿度传感器驱动（K10 板载，I2C 地址 0x38）
 * 本工程只取温度作为"液体温度"演示值
 */
#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_master_dev_handle_t dev;
} aht20_t;

/** @brief 初始化：软复位 + 校准（上电需执行一次校准命令） */
esp_err_t aht20_init(aht20_t *s, i2c_master_bus_handle_t bus);

/** @brief 触发一次测量并读取温度（阻塞约 90ms） */
esp_err_t aht20_read(aht20_t *s, float *temp_c, float *hum_pct);
