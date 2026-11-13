# -*- coding: utf-8 -*-
"""验收实验脚本：驱动 K10 真机，按验收标准逐项测试并记录 CSV。

实验序列：
  A 稳态精度：记录基线 30s，验证 |误差| ≤ 2mm
  B 设定值阶跃：SP 250→350，记录 150s（收敛过程）
  C 出水阀扰动：出水阀 30%→60%，记录 150s（PID 自动恢复）
  D 越界拒绝：写非法参数，验证被固件拒绝（读回值不变）
  （E 断网自持：需人工关热点配合，见提示）

数据保存到 实验数据/ 目录（utf-8-sig，Excel 直接打开）。
运行：python experiments.py <设备IP>
"""
import csv
import struct
import sys
import time

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

from pymodbus.client import ModbusTcpClient
from pymodbus.exceptions import ModbusException

import regs_map as rm

CSV_DIR = "实验数据"
HDR = ["时间", "液位mm", "设定值mm", "误差mm", "进水流量L/min", "出水流量L/min",
       "进水阀开度%", "出水阀开度%", "Kp", "Ki", "Kd", "PID周期ms", "温度℃", "运行状态"]

def f32_to_regs(v):
    return list(struct.unpack(">HH", struct.pack(">f", v)))

def f32(regs, a):
    return struct.unpack(">f", struct.pack(">HH", regs[a], regs[a + 1]))[0]

def read_all(c):
    hr = c.read_holding_registers(0, count=rm.HOLD_COUNT, device_id=1)
    ir = c.read_input_registers(0, count=rm.INP_COUNT, device_id=1)
    if hr.isError() or ir.isError():
        raise RuntimeError(f"读取失败 hr={hr} ir={ir}")
    return hr.registers, ir.registers

def write_f32(c, addr, v):
    r = c.write_registers(addr, f32_to_regs(v), device_id=1)
    if r.isError():
        raise RuntimeError(f"写入失败 @{addr}: {r}")

class Recorder:
    def __init__(self, name):
        import os
        os.makedirs(CSV_DIR, exist_ok=True)
        self.f = open(f"{CSV_DIR}/{name}", "w", newline="", encoding="utf-8-sig")
        self.w = csv.writer(self.f)
        self.w.writerow(HDR)

    def row(self, hr, ir):
        self.w.writerow([
            time.strftime("%H:%M:%S"),
            f"{f32(ir, rm.INP['LEVEL']):.2f}", f"{f32(ir, rm.INP['SP']):.2f}",
            f"{f32(ir, rm.INP['ERR']):.2f}",
            f"{f32(ir, rm.INP['QIN']):.3f}", f"{f32(ir, rm.INP['QOUT']):.3f}",
            f"{f32(ir, rm.INP['VALVE_IN']):.2f}", f"{f32(hr, rm.HOLD['VALVE_OUT']):.2f}",
            f"{f32(hr, rm.HOLD['Kp']):.4g}", f"{f32(hr, rm.HOLD['Ki']):.4g}",
            f"{f32(hr, rm.HOLD['Kd']):.4g}", hr[rm.HOLD['PID_MS']],
            f"{f32(ir, rm.INP['TEMP']):.1f}", ir[rm.INP['STATUS']]])
        self.f.flush()

    def close(self):
        self.f.close()

def record_for(c, rec, seconds, label):
    """采样记录 seconds 秒（2Hz），每秒打印一次进度"""
    end = time.time() + seconds
    last_print = 0
    while time.time() < end:
        hr, ir = read_all(c)
        rec.row(hr, ir)
        now = int(time.time())
        if now != last_print:
            last_print = now
            print(f"  [{label}] t+{seconds - (end - time.time()):.0f}s "
                  f"液位={f32(ir, rm.INP['LEVEL']):.1f}mm 误差={f32(ir, rm.INP['ERR']):+.2f}mm")
        time.sleep(0.5)

def main(ip):
    c = ModbusTcpClient(ip, port=502, timeout=2, retries=2)
    print(f"连接 {ip}:502 ...")
    try:
        hr, ir = read_all(c)
    except Exception as e:
        print(f"连接失败: {e}")
        print("提示：确认电脑与 K10 同一网络（热点），K10 屏幕应显示 IP")
        return 1
    print(f"已连接。当前 SP={f32(hr, rm.HOLD['SP']):.1f}mm "
          f"液位={f32(ir, rm.INP['LEVEL']):.1f}mm")

    # ---- A 稳态精度：基线 30s ----
    print("\n== 实验A：稳态精度（基线 30s）==")
    rec = Recorder("A_稳态基线.csv")
    record_for(c, rec, 30, "A")
    hr, ir = read_all(c)
    err = f32(ir, rm.INP['ERR'])
    print(f"稳态误差 = {err:+.2f} mm（验收要求 ≤±2mm）")
    assert abs(err) <= 2.0, "稳态误差超标！"
    rec.close()

    # ---- B 设定值阶跃 250→350 ----
    print("\n== 实验B：设定值阶跃 250→350（记录 150s）==")
    write_f32(c, rm.HOLD["SP"], 350.0)
    rec = Recorder("B_设定值阶跃.csv")
    record_for(c, rec, 150, "B")
    hr, ir = read_all(c)
    err = f32(ir, rm.INP['ERR'])
    print(f"阶跃后稳态误差 = {err:+.2f} mm")
    rec.close()

    # ---- C 出水阀扰动 30→60% ----
    print("\n== 实验C：出水阀扰动 30→60%（记录 150s，验证 PID 自动恢复）==")
    write_f32(c, rm.HOLD["VALVE_OUT"], 60.0)
    rec = Recorder("C_出水阀扰动.csv")
    record_for(c, rec, 150, "C")
    hr, ir = read_all(c)
    err = f32(ir, rm.INP['ERR'])
    print(f"扰动后稳态误差 = {err:+.2f} mm（应已恢复到 ±2mm 内）")
    rec.close()

    # ---- D 越界拒绝 ----
    print("\n== 实验D：越界写入拒绝 ==")
    tests = [
        ("设定液位 99999", rm.HOLD["SP"], 99999.0),
        ("出水阀 150%", rm.HOLD["VALVE_OUT"], 150.0),
        ("容器面积 0", rm.HOLD["AREA_CM2"], 0.0),
        ("PID周期 1ms", rm.HOLD["PID_MS"], 1),
    ]
    for name, addr, val in tests:
        hr_before, _ = read_all(c)
        if isinstance(val, int):
            c.write_registers(addr, [val], device_id=1)
            after = read_all(c)[0][addr]
        else:
            c.write_registers(addr, f32_to_regs(val), device_id=1)
            after = f32(read_all(c)[0], addr)
        before = hr_before[addr] if isinstance(val, int) else f32(hr_before, addr)
        ok = (after == before)
        print(f"  {'✓ 已拒绝' if ok else '✗ 未被拒绝!'} {name}: 写入后值 {after}（原值 {before}）")
        assert ok, f"越界参数未被拒绝: {name}"

    # ---- 恢复默认 ----
    print("\n恢复参数：SP=250，出水阀=30%")
    write_f32(c, rm.HOLD["SP"], 250.0)
    write_f32(c, rm.HOLD["VALVE_OUT"], 30.0)
    c.close()
    print("\n=== 实验 A~D 全部完成，数据在 实验数据/ 目录 ===")
    print("=== 实验E（断网自持）需人工配合：关手机热点 60s → 观察屏幕数值继续更新")
    print("=== → 重新打开热点 → 告诉我，我验证重连与数据连续性 ===")
    return 0

if __name__ == "__main__":
    ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.105.39"
    sys.exit(main(ip))
