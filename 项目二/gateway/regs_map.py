# -*- coding: utf-8 -*-
"""K10 液位控制站 Modbus 寄存器映射表（与固件 main/regs.h 保持一致）

约定：
- 地址全部为 0 基址（FC03/FC04 帧内地址），文档中的 4xxxx = 地址 + 40001
- FLOAT32 占 2 个寄存器，统一 ABCD 布局（word_order="big"，
  pymodbus convert_from_registers 默认值），与固件端完全一致
"""

# ---------------- 保持寄存器（可读写参数，FC03 读 / FC06、FC16 写）----------------
HOLD = {
    "SP":        0,    # 设定液位        FLOAT32  mm
    "Kp":        2,    # PID 比例        FLOAT32
    "Ki":        4,    # PID 积分        FLOAT32
    "Kd":        6,    # PID 微分        FLOAT32
    "PID_MS":    8,    # PID 周期        UINT16   ms
    "AREA_CM2":  9,    # 容器面积        FLOAT32  cm²
    "HEIGHT_MM": 11,   # 容器高度        FLOAT32  mm
    "QIN_MAX":   13,   # 进水阀最大流量  FLOAT32  L/min
    "QOUT_MAX":  15,   # 出水阀最大流量  FLOAT32  L/min
    "EXP_N":     17,   # 出水流量指数    FLOAT32  (Qout ∝ (h/H)^n)
    "SPEED":     19,   # 仿真速度倍率    FLOAT32  0.1~10
    "VALVE_OUT": 21,   # 出水阀开度      FLOAT32  %（A/B 键也修改此项）
    "RUN":       23,   # 运行控制        UINT16   0/1
    "SAVE":      24,   # 保存参数到 NVS  UINT16   写 1，完成自动回 0
    "CLEAR_I":   25,   # PID 积分清零    UINT16   写 1，完成自动回 0
}
HOLD_COUNT = 26

# ---------------- 输入寄存器（只读状态，FC04）----------------
INP = {
    "LEVEL":     0,    # 当前液位        FLOAT32  mm
    "SP":        2,    # 设定液位回读    FLOAT32  mm
    "ERR":       4,    # 误差 e=SP-h     FLOAT32  mm
    "VALVE_IN":  6,    # 进水阀开度      FLOAT32  %（PID 输出）
    "QIN":       8,    # 进水流量        FLOAT32  L/min
    "QOUT":      10,   # 出水流量        FLOAT32  L/min
    "TEMP":      12,   # 液体温度        FLOAT32  ℃（板载 AHT20）
    "PID_P":     14,   # PID P 分量      FLOAT32  %
    "PID_I":     16,   # PID I 分量      FLOAT32  %
    "PID_D":     18,   # PID D 分量      FLOAT32  %
    "PID_OUT":   20,   # PID 总输出      FLOAT32  %
    "STATUS":    22,   # 运行状态        UINT16   见 STATUS_BITS
}
INP_COUNT = 24

# STATUS 寄存器位定义
STATUS_BITS = {
    "PID_RUN":      0x01,   # bit0 PID 运行
    "AT_SETPOINT":  0x02,   # bit1 液位已到位（|e| <= 2mm）
    "MASTER_CONN":  0x04,   # bit2 主站已连接
    "WIFI_OK":      0x08,   # bit3 WiFi 已连接
}

# ---------------- 参数合法范围（客户端校验，与固件端一致）----------------
RANGES = {
    "SP":        (0.0, 1000.0),
    "Kp":        (0.0, 1000.0),
    "Ki":        (0.0, 1000.0),
    "Kd":        (0.0, 1000.0),
    "PID_MS":    (10, 5000),
    "AREA_CM2":  (10.0, 10000.0),
    "HEIGHT_MM": (50.0, 1000.0),
    "QIN_MAX":   (0.1, 100.0),
    "QOUT_MAX":  (0.1, 100.0),
    "EXP_N":     (0.0, 5.0),
    "SPEED":     (0.1, 10.0),
    "VALVE_OUT": (0.0, 100.0),
}
