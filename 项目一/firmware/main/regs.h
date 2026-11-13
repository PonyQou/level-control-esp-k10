/* Modbus 寄存器映射（0 基址），与上位机 pc_master/regs_map.py 严格一致
 * float32 占 2 个寄存器，ABCD 布局（高字在前，大端）
 */
#pragma once

/* ---- 保持寄存器（FC03 读 / FC06、FC16 写），共 26 ---- */
#define REG_SP         0    /* 设定液位        FLOAT32 mm */
#define REG_KP         2    /* FLOAT32 */
#define REG_KI         4    /* FLOAT32 */
#define REG_KD         6    /* FLOAT32 */
#define REG_PID_MS     8    /* PID 周期        UINT16  ms */
#define REG_AREA_CM2   9    /* 容器面积        FLOAT32 cm² */
#define REG_HEIGHT_MM  11   /* 容器高度        FLOAT32 mm */
#define REG_QIN_MAX    13   /* 进水阀最大流量  FLOAT32 L/min */
#define REG_QOUT_MAX   15   /* 出水阀最大流量  FLOAT32 L/min */
#define REG_EXP_N      17   /* 出水流量指数    FLOAT32 */
#define REG_SPEED      19   /* 仿真速度倍率    FLOAT32 */
#define REG_VALVE_OUT  21   /* 出水阀开度      FLOAT32 % */
#define REG_RUN        23   /* 运行控制        UINT16  0/1 */
#define REG_SAVE       24   /* 保存参数到 NVS  UINT16  写1 */
#define REG_CLEAR_I    25   /* PID 积分清零    UINT16  写1 */

#define HOLD_REG_COUNT 26

/* ---- 输入寄存器（FC04 只读），共 24 ---- */
#define IN_LEVEL       0    /* 当前液位   FLOAT32 mm */
#define IN_SP          2    /* 设定回读   FLOAT32 mm */
#define IN_ERR         4    /* 误差       FLOAT32 mm */
#define IN_VALVE_IN    6    /* 进水阀开度 FLOAT32 % */
#define IN_QIN         8    /* 进水流量   FLOAT32 L/min */
#define IN_QOUT        10   /* 出水流量   FLOAT32 L/min */
#define IN_TEMP        12   /* 液体温度   FLOAT32 ℃ */
#define IN_PID_P       14   /* PID P 分量 FLOAT32 % */
#define IN_PID_I       16   /* PID I 分量 FLOAT32 % */
#define IN_PID_D       18   /* PID D 分量 FLOAT32 % */
#define IN_PID_OUT     20   /* PID 总输出 FLOAT32 % */
#define IN_STATUS      22   /* 运行状态   UINT16 */

#define IN_REG_COUNT   24

/* IN_STATUS 位定义 */
#define STATUS_PID_RUN      (1u << 0)   /* bit0 PID 运行 */
#define STATUS_AT_SETPOINT  (1u << 1)   /* bit1 已到位（|e|<=2mm） */
#define STATUS_MASTER_CONN  (1u << 2)   /* bit2 主站已连接 */
#define STATUS_WIFI_OK      (1u << 3)   /* bit3 WiFi 已连接 */

/* ---- 参数合法范围（Modbus 写入校验用） ---- */
#define SP_MIN        0.0f
#define SP_MAX        1000.0f
#define KP_MIN        0.0f
#define KP_MAX        1000.0f
#define PID_MS_MIN    10
#define PID_MS_MAX    5000
#define AREA_MIN      10.0f
#define AREA_MAX      10000.0f
#define HEIGHT_MIN    50.0f
#define HEIGHT_MAX    1000.0f
#define QMAX_MIN      0.1f
#define QMAX_MAX      100.0f
#define EXP_N_MIN     0.0f
#define EXP_N_MAX     5.0f
#define SPEED_MIN     0.1f
#define SPEED_MAX     10.0f
#define VALVE_MIN     0.0f
#define VALVE_MAX     100.0f
