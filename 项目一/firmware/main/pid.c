#include "pid.h"

#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

void pid_init(pid_ctrl_t *p, float kp, float ki, float kd, uint32_t period_ms)
{
    p->kp = kp; p->ki = ki; p->kd = kd;
    p->period_ms = period_ms;
    p->integral = 0.0f;
    p->has_prev = false;
    p->p_term = p->i_term = p->d_term = p->output = 0.0f;
}

void pid_reset(pid_ctrl_t *p)
{
    /* 全清：积分清零命令/停止时执行，避免上位机看到陈旧的 P/I/D/输出显示 */
    p->integral = 0.0f;
    p->p_term = p->i_term = p->d_term = p->output = 0.0f;
    p->has_prev = false;   /* 停止→启动切换时防止陈旧微分冲击 */
}

float pid_update(pid_ctrl_t *p, float setpoint_mm, float level_mm, float dt_s)
{
    if (dt_s <= 0.0f) {
        return p->output;
    }

    float err = setpoint_mm - level_mm;

    /* P 项 */
    p->p_term = p->kp * err;

    /* I 项（抗积分饱和：输出饱和且误差方向不利于退出时不积分） */
    if (!((p->output >= 100.0f && err > 0.0f) ||
          (p->output <= 0.0f   && err < 0.0f))) {
        p->integral += p->ki * err * dt_s;
    }
    p->integral = CLAMP(p->integral, -100.0f, 100.0f);
    p->i_term = p->integral;

    /* D 项（微分先行：对测量值微分，避免设定值阶跃冲击） */
    if (p->has_prev) {
        p->d_term = -p->kd * (level_mm - p->prev_level) / dt_s;
    } else {
        p->d_term = 0.0f;
        p->has_prev = true;
    }
    p->prev_level = level_mm;

    /* 输出限幅 0~100% */
    p->output = CLAMP(p->p_term + p->i_term + p->d_term, 0.0f, 100.0f);
    return p->output;
}
