/* 纯 LVGL 界面核心（UI 规格契约 V1 实现，不依赖 ESP-IDF）
 * 真机（ui.c）与 PC 模拟器（ui_sim）共用。契约为准，图片只做人工验收。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* 状态位（与 regs.h 的 STATUS_* 保持一致） */
#define UISTAT_PID_RUN      (1u << 0)
#define UISTAT_AT_SETPOINT  (1u << 1)
#define UISTAT_MASTER_CONN  (1u << 2)
#define UISTAT_WIFI_OK      (1u << 3)

/* 契约色值（16 进制，与 UI 规格契约 V1 一一对应） */
#define UI_C_BG        0x222222   /* bg_main 深炭灰 */
#define UI_C_CARD      0x333333   /* card_bg */
#define UI_C_LINE      0x444444   /* line_div */
#define UI_C_TITLE     0xFFFFFF   /* text_title 白 */
#define UI_C_LABEL     0xD8D8D8   /* text_label 浅灰 */
#define UI_C_VALUE     0x39E8F5   /* text_value 荧光青 */
#define UI_C_OK        0x36D166   /* light_ok 亮绿 */
#define UI_C_WAIT      0xFFC145   /* light_wait 琥珀黄 */
#define UI_C_ERR       0xFF4C4C   /* light_err 红 */

typedef struct {
    float    level_mm;        /* 当前液位 */
    float    setpoint_mm;     /* 设定液位 */
    float    height_mm;       /* 容器高度（液面比例 = level/height） */
    float    valve_in_pct;    /* 进水阀开度 % */
    float    valve_out_pct;   /* 出水阀开度 % */
    float    flow_in;         /* 进水流量 L/min */
    float    flow_out;        /* 出水流量 L/min */
    float    temp_c;          /* 液体温度 ℃ */
    uint16_t status;          /* UISTAT_* 位 */
    char     ip_str[16];      /* 设备 IP */
    bool     data_valid;      /* 数据有效（无效时数值显示 --.-- 灰色） */
} ui_snapshot_t;

/** @brief 创建界面（用户修正版：全界面统一 20px 字体，
 *  颜色分层保留：标题白 / 标签浅灰 / 数值荧光青） */
void ui_core_create(lv_obj_t *screen, const lv_font_t *font);

/** @brief 用快照刷新界面（调用方负责 LVGL 锁） */
void ui_core_update(const ui_snapshot_t *s);

/** @brief 导出对象树（模拟器布局断言用） */
void ui_core_dump_tree(void);
