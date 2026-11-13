#include "tank_model.h"
#include <math.h>

#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

void tank_init(tank_t *t)
{
    t->area_cm2      = 200.0f;
    t->height_mm     = 500.0f;
    t->qin_max       = 8.0f;
    t->qout_max      = 4.0f;
    t->exp_n         = 0.5f;
    t->sim_speed     = 1.0f;
    t->valve_out_pct = 30.0f;

    t->level_mm     = 0.0f;
    t->valve_in_pct = 0.0f;
    t->flow_in      = 0.0f;
    t->flow_out     = 0.0f;
    t->running      = true;
}

void tank_step(tank_t *t, float dt_s)
{
    if (!t->running || dt_s <= 0.0f) {
        return;
    }

    float a_m2  = t->area_cm2 / 10000.0f;               /* cm² -> m² */
    float u     = CLAMP(t->valve_in_pct / 100.0f, 0.0f, 1.0f);
    float v     = CLAMP(t->valve_out_pct / 100.0f, 0.0f, 1.0f);
    float h_n   = CLAMP(t->level_mm / t->height_mm, 0.0f, 1.0f);

    float qin   = u * t->qin_max;                       /* L/min */
    float qout  = v * t->qout_max * powf(h_n, t->exp_n);
    t->flow_in  = qin;
    t->flow_out = qout;

    /* dh[mm] = Q[L/min] × dt[s] / 60[s/min] / A[m²]
     * 单位推导：Q[L/min]/60 → L/s → ×0.001 m³/L → /A[m²] → m/s → ×1000 mm/m
     * L→m³ 的 0.001 与 m→mm 的 1000 恰好抵消，最终公式不带系数 */
    float dh = (qin - qout) / 60.0f / a_m2 * dt_s * t->sim_speed;
    t->level_mm = CLAMP(t->level_mm + dh, 0.0f, t->height_mm);
}
