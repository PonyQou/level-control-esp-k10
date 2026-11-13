/* LCD 界面模块对外接口（板级初始化 + 刷新）
 * 快照类型与纯界面代码见 ui_core.h（与 PC 模拟器共用）
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "ui_core.h"

/** @brief 初始化 LCD（ILI9341 + LVGL），创建界面对象。在 app_main 调用 */
esp_err_t ui_init(void);

/** @brief 刷新界面（控制任务每 100ms 调用一次，内部带 LVGL 锁） */
void ui_update(const ui_snapshot_t *s);
