# -*- coding: utf-8 -*-
"""实验数据 CSV 记录器。

要点：
- encoding="utf-8-sig"（带 BOM）：Windows Excel 双击打开中文不乱码
- newline=""：防止 Windows 下出现多余空行
- 每写一行立即 flush()：采样率低（2Hz），保证断电/异常退出也不丢数据
"""
import csv
import time

# 表头覆盖课程要求：时间、液位、设定值、误差、进出水流量、阀门开度和 PID 分项
HEADER = [
    "时间", "液位mm", "设定值mm", "误差mm",
    "进水流量L/min", "出水流量L/min", "进水阀开度%", "出水阀开度%",
    "Kp", "Ki", "Kd", "PID周期ms", "温度℃", "运行状态",
]


class DataLogger:
    def __init__(self, path: str):
        self._f = open(path, "w", newline="", encoding="utf-8-sig")
        self._w = csv.writer(self._f)
        self._w.writerow(HEADER)
        self._f.flush()

    def write(self, level, sp, err, qin, qout, vin, vout,
              kp, ki, kd, pid_ms, temp, status):
        self._w.writerow([
            time.strftime("%Y-%m-%d %H:%M:%S"),
            f"{level:.2f}", f"{sp:.2f}", f"{err:.2f}",
            f"{qin:.3f}", f"{qout:.3f}", f"{vin:.2f}", f"{vout:.2f}",
            f"{kp:.4g}", f"{ki:.4g}", f"{kd:.4g}",
            int(pid_ms), f"{temp:.1f}", int(status),
        ])
        self._f.flush()

    def close(self):
        self._f.close()
