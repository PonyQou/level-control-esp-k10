/* XL9535 16 位 I2C IO 扩展器驱动（K10 板载，地址 0x20）
 * 本工程用途：LCD 背光控制（P00 输出）、按键 A/B 读取（P05/P11 输入）
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_master_dev_handle_t dev;   /* I2C 设备句柄（地址 0x20） */
} xl9535_t;

/**
 * @brief 初始化：P00 配置为输出并点亮背光，P05/P11 配置为输入（按键），
 *        其余引脚保持输入（避免与板载资源冲突）
 * @param bus 由 app 层创建的 I2C 总线句柄（GPIO47/48）
 */
esp_err_t xl9535_init(xl9535_t *io, i2c_master_bus_handle_t bus);

/** @brief 背光开关（P00，高有效） */
esp_err_t xl9535_backlight(xl9535_t *io, bool on);

/** @brief 读取按键 A（P05），返回 true 表示按下（低电平有效） */
bool xl9535_key_a(xl9535_t *io);

/** @brief 读取按键 B（P11），返回 true 表示按下 */
bool xl9535_key_b(xl9535_t *io);

/** @brief 读取两个输入端口原始值（调试用） */
esp_err_t xl9535_read_inputs(xl9535_t *io, uint8_t *port0, uint8_t *port1);
