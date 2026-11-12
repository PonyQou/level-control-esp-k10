# -*- coding: utf-8 -*-
"""K10 模拟从站（开发用，硬件不在场时联调上位机）。

pymodbus 3.16 新模拟器框架要点（踩坑记录，详见 调研纪要）：
- 旧 ModbusSlaveContext/ModbusSequentialDataBlock(setValues) 已弃用（v4 将删除），
  官方推荐 SimData + SimDevice
- SimDevice 的 simdata 是四元组，每项都是 SimData 列表：(线圈, 离散量, 保持, 输入)
- SimCore 在服务器启动时把 SimData.values 拷贝进运行时寄存器，此后两者脱钩：
  后台线程改 SimData.values 无效；FC16 写入会直接落在运行时寄存器上并持久化
- 动态数据的正确做法：给 SimDevice 挂 async action 回调，
  读输入寄存器(FC4)时按时间步长计算模型并把新值写进 current_registers；
  写保持寄存器(FC6/FC16)时同步更新内部参数状态

运行：python mock_slave.py  （端口 502，从站地址 1）
"""
import struct
import time

from pymodbus.simulator import SimData, SimDevice, DataType
from pymodbus.server import StartTcpServer

import regs_map as rm

def f32_to_regs(v):
    return list(struct.unpack(">HH", struct.pack(">f", v)))

def regs_to_f32(hi, lo):
    return struct.unpack(">f", struct.pack(">HH", hi, lo))[0]

# ---------- 初始参数（与固件默认值一致） ----------
HOLD_DEFAULT = {
    "SP": 250.0, "Kp": 3.0, "Ki": 0.05, "Kd": 0.5, "PID_MS": 200,
    "AREA_CM2": 200.0, "HEIGHT_MM": 500.0, "QIN_MAX": 8.0, "QOUT_MAX": 4.0,
    "EXP_N": 0.5, "SPEED": 1.0, "VALVE_OUT": 30.0, "RUN": 1,
}


def build_hold_regs():
    regs = [0] * rm.HOLD_COUNT
    def put_f32(key, v):
        hi, lo = f32_to_regs(v)
        regs[rm.HOLD[key]] = hi
        regs[rm.HOLD[key] + 1] = lo
    for k, v in HOLD_DEFAULT.items():
        if k in ("PID_MS", "RUN", "SAVE", "CLEAR_I"):
            regs[rm.HOLD[k]] = int(v)
        else:
            put_f32(k, v)
    return regs


class TankSim:
    """液位模型：一阶惯性逼近设定值。每次 FC4 读取时按真实时间步长推进"""

    def __init__(self):
        self.hr_state = build_hold_regs()   # 参数状态（随 FC16 写入同步更新）
        self.level = 0.0
        self.temp = 26.0
        self.last_t = time.monotonic()

    def on_hold_write(self, address, count, values):
        """FC06/FC16 写入保持寄存器：更新内部参数状态
        注意：与固件不同，mock 不做写入拒绝，而是按 RANGES 钳位
        （防止主站写 AREA=0 之类导致除零崩溃）"""
        self.hr_state[address:address + count] = values
        # 对 float32 参数逐个钳位到合法范围
        for key, (lo, hi) in rm.RANGES.items():
            if key == "PID_MS":
                v = self.hr_state[rm.HOLD[key]]
                self.hr_state[rm.HOLD[key]] = max(lo, min(hi, v))
            else:
                off = rm.HOLD[key]
                v = regs_to_f32(self.hr_state[off], self.hr_state[off + 1])
                if not (lo <= v <= hi):
                    self.hr_state[off], self.hr_state[off + 1] = f32_to_regs(
                        max(lo, min(hi, v)))

    def step_and_fill(self, ir_regs):
        """推进模型并把全部输入寄存器写进 ir_regs（运行时输入寄存器块）"""
        now = time.monotonic()
        dt = min(now - self.last_t, 5.0)    # 限制单步，防止调试停顿后跳变
        self.last_t = now
        h = self.hr_state

        sp = regs_to_f32(h[rm.HOLD["SP"]], h[rm.HOLD["SP"] + 1])
        vout = regs_to_f32(h[rm.HOLD["VALVE_OUT"]], h[rm.HOLD["VALVE_OUT"] + 1])
        area = regs_to_f32(h[rm.HOLD["AREA_CM2"]], h[rm.HOLD["AREA_CM2"] + 1])
        speed = regs_to_f32(h[rm.HOLD["SPEED"]], h[rm.HOLD["SPEED"] + 1])
        height = regs_to_f32(h[rm.HOLD["HEIGHT_MM"]], h[rm.HOLD["HEIGHT_MM"] + 1])
        qmax_in = regs_to_f32(h[rm.HOLD["QIN_MAX"]], h[rm.HOLD["QIN_MAX"] + 1])
        qmax_out = regs_to_f32(h[rm.HOLD["QOUT_MAX"]], h[rm.HOLD["QOUT_MAX"] + 1])
        exp_n = regs_to_f32(h[rm.HOLD["EXP_N"]], h[rm.HOLD["EXP_N"] + 1])
        kp = regs_to_f32(h[rm.HOLD["Kp"]], h[rm.HOLD["Kp"] + 1])
        run = h[rm.HOLD["RUN"]]

        # 比例阀位 + 出水流量（与固件 tank_model.c 公式严格一致）
        err = sp - self.level
        vin = max(0.0, min(100.0, kp * err)) if run else 0.0
        qin = vin / 100.0 * qmax_in
        qout = vout / 100.0 * qmax_out * (max(self.level, 0) / height) ** exp_n
        a_m2 = area / 10000.0
        # dh[mm] = Q[L/min]×dt[s] / 60 / A[m²]（L→m³ 与 m→mm 系数抵消）
        self.level += dt * speed * (qin - qout) / 60.0 / a_m2
        self.level = max(0.0, min(self.level, height))
        self.temp = 26.0 + 8.0 * (1 - self.level / max(height, 1))

        vals = [0] * rm.INP_COUNT
        def put_f32(key, v):
            hi, lo = f32_to_regs(v)
            vals[rm.INP[key]] = hi
            vals[rm.INP[key] + 1] = lo
        put_f32("LEVEL", self.level)
        put_f32("SP", sp)
        put_f32("ERR", err)
        put_f32("VALVE_IN", vin)
        put_f32("QIN", qin)
        put_f32("QOUT", qout)
        put_f32("TEMP", self.temp)
        put_f32("PID_P", kp * err)
        put_f32("PID_I", 0.3 * err)
        put_f32("PID_D", 0.0)
        put_f32("PID_OUT", vin)
        status = rm.STATUS_BITS["PID_RUN"] | rm.STATUS_BITS["WIFI_OK"] \
            | rm.STATUS_BITS["MASTER_CONN"]
        if run and abs(err) <= 2.0:
            status |= rm.STATUS_BITS["AT_SETPOINT"]
        vals[rm.INP["STATUS"]] = status
        # 注意：运行时寄存器列表末尾有一个哨兵元素（DataType.INVALID 越界保护），
        # 只能赋值前 len(vals) 个，不能整体切片赋值
        ir_regs[:len(vals)] = vals


def main():
    tank = TankSim()

    async def dev_action(func_code, start_address, address, count,
                         current_registers, set_values):
        """SimDevice 访问回调：读输入寄存器时刷新模型；写保持寄存器时同步参数"""
        if set_values is not None and func_code in (6, 16):    # 写保持寄存器
            tank.on_hold_write(address - start_address, count, set_values)
        elif func_code == 4:                                   # 读输入寄存器
            tank.step_and_fill(current_registers)
        return None

    hr = SimData(0, values=build_hold_regs(), datatype=DataType.REGISTERS)
    ir = SimData(0, values=[0] * rm.INP_COUNT, datatype=DataType.REGISTERS)
    co = SimData(0, values=[False] * 8, datatype=DataType.BITS)
    di = SimData(0, values=[False], datatype=DataType.BITS)
    # SimDevice 要求 simdata 为四元组，且每项都是 SimData 列表：(线圈, 离散量, 保持, 输入)
    device = SimDevice(0, simdata=([co], [di], [hr], [ir]), action=dev_action)

    print("K10 模拟从站已启动: 127.0.0.1:502, 从站地址 1")
    print("用上位机连接: python main.py (IP 填 127.0.0.1)")
    StartTcpServer(context=device, address=("0.0.0.0", 502))


if __name__ == "__main__":
    main()
