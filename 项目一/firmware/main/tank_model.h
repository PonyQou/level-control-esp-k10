/* 液位容器仿真模型
 * 带进水阀（PID 控制）和出水阀（按键/Modbus 可调）的圆柱容器，离散化欧拉法：
 *   Qin  = u × QmaxIn                     u = 进水阀开度 0~1（PID 输出）
 *   Qout = v × QmaxOut × (h/H)^n          v = 出水阀开度 0~1, n = 出水流量指数
 *   h[k+1] = h[k] + dt × speed × (Qin − Qout) / A
 * 单位：液位 mm、流量 L/min、面积 cm²、时间 s
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* ---- 可配置参数（Modbus 可写，单位见注释） ---- */
    float area_cm2;        /* 容器面积 cm² */
    float height_mm;       /* 容器高度 mm */
    float qin_max;         /* 进水阀最大流量 L/min */
    float qout_max;        /* 出水阀最大流量 L/min */
    float exp_n;           /* 出水流量指数 Qout ∝ (h/H)^n */
    float sim_speed;       /* 仿真速度倍率 */
    float valve_out_pct;   /* 出水阀开度 %（按键 A/B 调整，Modbus 可写） */

    /* ---- 运行状态（每周期更新，Modbus 可读） ---- */
    float level_mm;        /* 当前液位 mm */
    float valve_in_pct;    /* 进水阀开度 %（PID 输出） */
    float flow_in;         /* 进水流量 L/min */
    float flow_out;        /* 出水流量 L/min */

    bool  running;         /* 运行/停止 */
} tank_t;

/** @brief 用默认参数初始化（与上位机 mock 默认值一致） */
void tank_init(tank_t *t);

/** @brief 推进一个时间步 dt（秒）。qin 由外部（PID）写入 valve_in_pct 后调用 */
void tank_step(tank_t *t, float dt_s);
