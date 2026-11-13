# -*- coding: utf-8 -*-
"""Modbus TCP → OPC UA 协议网关（项目二）

- Modbus 侧：AsyncModbusTcpClient 周期 500ms 读 K10 全部寄存器
- OPC UA 侧：asyncua 服务器，加载官方 DI NodeSet，TankType 子类型化 DeviceType
- 双向转换：寄存器 uint16 对 ↔ 工程 float（ABCD 布局，与项目一一致）
- UA 写参数：轮询差分检测 → 范围校验 → FC16 写回 K10（越界恢复原值 + 记录错误）
- 诊断变量：ModbusConnected / LastError / PollCount

运行：python gateway.py [K10_IP] [UA端口=4840]
"""
import asyncio
import struct
import sys
import traceback
from datetime import datetime

from asyncua import Server, ua
from asyncua.common.methods import uamethod
from pymodbus.client import AsyncModbusTcpClient
from pymodbus.exceptions import ModbusException

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

import regs_map as rm

UA_NS = "http://k10-gateway.org/UA"
DI_NS = "http://opcfoundation.org/UA/DI/"
NODESET_DI = "nodesets/Opc.Ua.Di.NodeSet2.xml"
DI_DEVICE_TYPE = 1002                 # DeviceType 在 DI NodeSet 中的 NodeId
POLL_MS = 0.5

def f32_to_regs(v):
    return list(struct.unpack(">HH", struct.pack(">f", v)))

def regs_to_f32(hi, lo):
    return struct.unpack(">f", struct.pack(">HH", hi, lo))[0]

# UA 变量 → (寄存器组, rm 地址, 是否 uint16, 范围键)
MODEL = {
    # 只读（输入寄存器）
    "Level":       ("in", rm.INP["LEVEL"],     False, None),
    "Error":       ("in", rm.INP["ERR"],       False, None),
    "ValveIn":     ("in", rm.INP["VALVE_IN"],  False, None),
    "FlowIn":      ("in", rm.INP["QIN"],       False, None),
    "FlowOut":     ("in", rm.INP["QOUT"],      False, None),
    "Temperature": ("in", rm.INP["TEMP"],      False, None),
    # 可读写（保持寄存器）
    "Setpoint":    ("hold", rm.HOLD["SP"],         False, "SP"),
    "ValveOut":    ("hold", rm.HOLD["VALVE_OUT"],  False, "VALVE_OUT"),
    "Kp":          ("hold", rm.HOLD["Kp"],         False, "Kp"),
    "Ki":          ("hold", rm.HOLD["Ki"],         False, "Ki"),
    "Kd":          ("hold", rm.HOLD["Kd"],         False, "Kd"),
    "PIDPeriod":   ("hold", rm.HOLD["PID_MS"],     True,  "PID_MS"),
    "Area":        ("hold", rm.HOLD["AREA_CM2"],   False, "AREA_CM2"),
    "Height":      ("hold", rm.HOLD["HEIGHT_MM"],  False, "HEIGHT_MM"),
    "QinMax":      ("hold", rm.HOLD["QIN_MAX"],    False, "QIN_MAX"),
    "QoutMax":     ("hold", rm.HOLD["QOUT_MAX"],   False, "QOUT_MAX"),
    "ExpN":        ("hold", rm.HOLD["EXP_N"],      False, "EXP_N"),
    "Speed":       ("hold", rm.HOLD["SPEED"],      False, "SPEED"),
}


class TankGateway:
    def __init__(self, modbus_host, ua_port=4840):
        self.modbus_host = modbus_host
        self.ua_port = ua_port
        self.server = Server()
        self.mb = None   # AsyncModbusTcpClient 必须在事件循环内创建
        self.vars = {}            # UA 名 -> 变量节点
        self.var_types = {}       # UA 名 -> VariantType（写值需显式类型）
        self.rw_names = []        # 可写变量名列表
        self.write_queue = asyncio.Queue()   # 方法触发的写请求
        self.mb_connected = False
        self.initialized = False      # 首次成功读后，先用 Modbus 值初始化 UA 变量
        self.prev = {}                # name -> (mod_val, ua_val) 上周期快照，用于区分变更来源
        self.last_error = ""
        self.poll_count = 0
        self._idx = None
        self.tank = None

    # ---------------- UA 信息模型 ----------------
    async def build_model(self):
        await self.server.init()
        self.server.set_endpoint(f"opc.tcp://0.0.0.0:{self.ua_port}")
        self.server.set_server_name("K10 Liquid Level Gateway")
        self._idx = await self.server.register_namespace(UA_NS)

        # 导入官方 DI companion NodeSet（DeviceType = ns;i=1002，导入时自动映射）
        # strict_mode=False：DI 1.0 的少量废弃参数节点引用缺失，不影响 DeviceType 层级
        await self.server.import_xml(NODESET_DI, strict_mode=False)
        di_ns = await self.server.get_namespace_index(DI_NS)
        device_type = self.server.get_node(ua.NodeId(DI_DEVICE_TYPE, di_ns))

        # TankType：DeviceType 的子类型（DeviceType HasSubtype TankType）
        ns = self._idx
        tank_type = await self.server.nodes.base_object_type.add_object_type(
            ns, "TankType")
        await device_type.add_reference(tank_type, ua.ObjectIds.HasSubtype,
                                        forward=True, bidirectional=False)

        # DI 规范标准属性
        await tank_type.add_property(ns, "SerialNumber",
                                     ua.Variant("K10-001", ua.VariantType.String))
        await tank_type.add_property(ns, "Manufacturer",
                                     ua.Variant("DFRobot", ua.VariantType.String))
        await tank_type.add_property(ns, "Model",
                                     ua.Variant("UNIHIKER K10", ua.VariantType.String))

        # 变量：声明挂在 TankType（建模），实例声明挂 Tank1（运行）
        # （asyncua 的 add_object 不会自动复制类型子节点，需手动双挂）
        def default_val(vtype):
            # Variant 初值必须与类型严格匹配（如 Boolean 传 0 会导致 BadTypeMismatch）
            return {ua.VariantType.Boolean: False,
                    ua.VariantType.String: "",
                    ua.VariantType.UInt16: 0,
                    ua.VariantType.UInt32: 0}.get(vtype, 0.0)

        async def add_var(parent, name, vtype, rw):
            return await parent.add_variable(
                ns, name, ua.Variant(default_val(vtype), vtype),
                ua.AccessLevel.CurrentWrite if rw else ua.AccessLevel.CurrentRead)

        for name, (grp, addr, is_u16, rkey) in MODEL.items():
            rw = (grp == "hold")
            vtype = ua.VariantType.UInt16 if is_u16 else ua.VariantType.Double
            await add_var(tank_type, name, vtype, rw)   # 类型声明
            if rw:
                self.rw_names.append(name)

        for d, vtype in (("ModbusConnected", ua.VariantType.Boolean),
                         ("LastError", ua.VariantType.String),
                         ("PollCount", ua.VariantType.UInt32)):
            await add_var(tank_type, d, vtype, False)

        # 方法（类型 + 实例双挂，保证客户端对实例调用可用）
        await self._add_methods(tank_type)

        # Tank1 实例
        self.tank = await self.server.nodes.objects.add_object(
            ns, "Tank1", tank_type.nodeid)
        for name, (grp, addr, is_u16, rkey) in MODEL.items():
            rw = (grp == "hold")
            vtype = ua.VariantType.UInt16 if is_u16 else ua.VariantType.Double
            self.vars[name] = await add_var(self.tank, name, vtype, rw)
            self.var_types[name] = vtype
            await self.vars[name].set_writable(rw)
        for d, vtype in (("ModbusConnected", ua.VariantType.Boolean),
                         ("LastError", ua.VariantType.String),
                         ("PollCount", ua.VariantType.UInt32)):
            self.vars[d] = await add_var(self.tank, d, vtype, False)
            self.var_types[d] = vtype
            await self.vars[d].set_writable(False)
        await self._add_methods(self.tank)

    async def _add_methods(self, tank_type):
        ns = self._idx

        @uamethod
        async def SetSetpoint(parent, value: float):
            lo, hi = rm.RANGES["SP"]
            if not (lo <= value <= hi):
                return [ua.StatusCode(ua.StatusCodes.BadOutOfRange)]
            print(f"[方法] SetSetpoint({value})", flush=True)
            self.write_queue.put_nowait(("Setpoint", value))
            return [ua.StatusCode(ua.StatusCodes.Good)]

        @uamethod
        async def SetValveOut(parent, value: float):
            lo, hi = rm.RANGES["VALVE_OUT"]
            if not (lo <= value <= hi):
                return [ua.StatusCode(ua.StatusCodes.BadOutOfRange)]
            print(f"[方法] SetValveOut({value})", flush=True)
            self.write_queue.put_nowait(("ValveOut", value))
            return [ua.StatusCode(ua.StatusCodes.Good)]

        @uamethod
        async def SaveParams(parent):
            self.write_queue.put_nowait(("SAVE", 1))
            return [ua.StatusCode(ua.StatusCodes.Good)]

        @uamethod
        async def ClearIntegral(parent):
            self.write_queue.put_nowait(("CLEAR_I", 1))
            return [ua.StatusCode(ua.StatusCodes.Good)]

        @uamethod
        async def Start(parent):
            self.write_queue.put_nowait(("RUN", 1))
            return [ua.StatusCode(ua.StatusCodes.Good)]

        @uamethod
        async def Stop(parent):
            self.write_queue.put_nowait(("RUN", 0))
            return [ua.StatusCode(ua.StatusCodes.Good)]

        for m, nargs in ((SetSetpoint, 1), (SetValveOut, 1), (SaveParams, 0),
                          (ClearIntegral, 0), (Start, 0), (Stop, 0)):
            await tank_type.add_method(ns, m.__name__, m,
                                       [ua.VariantType.Double] * nargs,
                                       [ua.VariantType.StatusCode])

    # ---------------- 诊断辅助 ----------------
    async def _set_error(self, msg):
        self.last_error = msg
        await self.vars["LastError"].write_value(
            f"{datetime.now():%H:%M:%S} {msg}")

    # ---------------- Modbus 轮询主循环 ----------------
    async def modbus_loop(self):
        while True:
            try:
                if not self.mb.connected:
                    await self.mb.connect()
                hr = await self.mb.read_holding_registers(0, count=rm.HOLD_COUNT, device_id=1)
                ir = await self.mb.read_input_registers(0, count=rm.INP_COUNT, device_id=1)
                if hr.isError() or ir.isError():
                    raise ModbusException(f"从站异常 hr={hr.isError()} ir={ir.isError()}")
                h, i = hr.registers, ir.registers

                # 寄存器 → UA 变量（初始化阶段直接回填；之后差分检测 UA 写）
                for name, (grp, addr, is_u16, rkey) in MODEL.items():
                    regs = h if grp == "hold" else i
                    mod_val = (regs[addr] if is_u16
                               else regs_to_f32(regs[addr], regs[addr + 1]))
                    ua_val = await self.vars[name].read_value()
                    if not self.initialized:
                        # 初始化：若客户端在首轮前已写入（值≠类型默认），保留并交给下周期差分处理
                        if grp == "hold" and ua_val != mod_val and ua_val != 0:
                            self.prev[name] = (mod_val, ua_val)
                        else:
                            await self._safe_set(name, mod_val)
                            self.prev[name] = (mod_val, mod_val)
                        continue
                    prev_mod, prev_ua = self.prev.get(name, (None, None))
                    mod_changed = (prev_mod is not None) and (mod_val != prev_mod)
                    ua_changed = (prev_ua is not None) and (ua_val != prev_ua)
                    if prev_mod is None:
                        self.prev[name] = (mod_val, ua_val)
                    elif mod_changed and not ua_changed:
                        # 外部改了 Modbus（K10 按键/上位机写）→ UA 跟随，绝不写回；
                        # 关键：立即同步 prev_ua，防止下周期被误判为 UA 客户端写（F1 修复）
                        await self._safe_set(name, mod_val)
                        self.prev[name] = (mod_val, mod_val)
                    elif ua_changed and not mod_changed:
                        # UA 客户端改了变量 → 校验并写回 Modbus
                        if grp != "hold":
                            await self._safe_set(name, mod_val)
                            self.prev[name] = (mod_val, mod_val)
                        elif rkey and not (rm.RANGES[rkey][0] <= ua_val <= rm.RANGES[rkey][1]):
                            await self._safe_set(name, mod_val)   # 恢复原值
                            self.prev[name] = (mod_val, mod_val)
                            await self._set_error(f"{name}={ua_val} 越界被拒")
                        else:
                            await self._mb_write(name, addr, is_u16, ua_val)
                            self.prev[name] = (mod_val, ua_val)
                    elif ua_val != mod_val:
                        # 两侧同时变化但不一致（罕见）：以设备（Modbus）为准
                        await self._safe_set(name, mod_val)
                        self.prev[name] = (mod_val, mod_val)
                    else:
                        self.prev[name] = (mod_val, ua_val)

                # 方法写队列
                while not self.write_queue.empty():
                    name, value = self.write_queue.get_nowait()
                    print(f"[写队列] {name}={value}", flush=True)
                    if name in ("SAVE", "CLEAR_I", "RUN"):
                        r = await self.mb.write_registers(rm.HOLD[name], [int(value)],
                                                          device_id=1)
                        if r.isError():
                            await self._set_error(f"方法 {name} 写失败: {r}")
                    else:
                        grp, addr, is_u16, _ = MODEL[name]
                        await self._mb_write(name, addr, is_u16, float(value))

                if not self.initialized:
                    self.initialized = True
                if not self.mb_connected:
                    self.mb_connected = True
                    await self._safe_set("ModbusConnected", True)
                    await self._safe_set("LastError", "")
                    self.last_error = ""
                self.poll_count += 1
                await self._safe_set("PollCount", self.poll_count)

            except Exception as exc:
                if self.mb_connected:
                    self.mb_connected = False
                    try:
                        await self._safe_set("ModbusConnected", False)
                    except Exception:
                        pass
                print("=== 轮询异常回溯 ===", flush=True)
                traceback.print_exc()
                await self._set_error(str(exc))
            await asyncio.sleep(POLL_MS)

    def _ua_val(self, name, value):
        """按变量声明的类型构造显式 Variant（Python int 不会自动匹配
        UInt16/UInt32，隐式推断会抛 BadTypeMismatch）"""
        vt = self.var_types.get(name)
        if vt in (ua.VariantType.UInt16, ua.VariantType.UInt32):
            return ua.Variant(int(value), vt)
        return value

    async def _safe_set(self, name, value):
        """写 UA 变量；失败时记录变量名（BadTypeMismatch 定位用）"""
        try:
            await self.vars[name].write_value(self._ua_val(name, value))
        except Exception as exc:
            await self._set_error(f"UA 变量 {name} 写入失败: {exc}")

    async def _mb_write(self, name, addr, is_u16, value):
        regs = ([int(value)] if is_u16 else f32_to_regs(float(value)))
        r = await self.mb.write_registers(addr, regs, device_id=1)
        if r.isError():
            await self._set_error(f"写 {name} 失败: {r}")
        elif name in self.vars:
            # 同步 UA 变量：防止差分逻辑把网关自己的写入误判为客户端修改
            await self._safe_set(name, value)

    async def run(self):
        self.mb = AsyncModbusTcpClient(self.modbus_host, port=502, timeout=2)
        await self.build_model()
        await self.server.start()
        print(f"UA 服务器: opc.tcp://0.0.0.0:{self.ua_port}")
        print(f"Modbus 轮询: {self.modbus_host}:502 @ {POLL_MS}s")
        try:
            await self.modbus_loop()
        finally:
            await self.server.stop()


if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "192.168.105.39"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 4840
    asyncio.run(TankGateway(host, port).run())
