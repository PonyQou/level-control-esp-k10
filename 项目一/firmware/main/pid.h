/* PID 控制器（位置式 + 抗积分饱和 + 微分先行）
 * - 输出为进水阀开度 0~100 %
 * - 微分作用于测量值（-Kd·dLevel/dt），设定值阶跃时无微分冲击
 * - 积分仅在输出未饱和（或误差方向利于退出饱和）时累加，防止 windup
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* ---- 参数（Modbus 可写） ---- */
    float    kp, ki, kd;
    uint32_t period_ms;    /* PID 计算周期 */

    /* ---- 内部状态 ---- */
    float    integral;
    float    prev_level;
    bool     has_prev;

    /* ---- 诊断输出（Modbus 可读，单位 %） ---- */
    float    p_term, i_term, d_term, output;
} pid_ctrl_t;

void  pid_init(pid_ctrl_t *p, float kp, float ki, float kd, uint32_t period_ms);
/** @brief 单步计算。dt_s 为距上次计算的时间（秒）。返回进水阀开度 % */
float pid_update(pid_ctrl_t *p, float setpoint_mm, float level_mm, float dt_s);
/** @brief 积分清零（Modbus 清零积分命令用） */
void  pid_reset(pid_ctrl_t *p);
