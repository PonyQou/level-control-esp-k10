# -*- coding: utf-8 -*-
"""Modbus 轮询工作线程。

关键设计（pymodbus 官方文档明确 ModbusTcpClient 非线程安全）：
- 客户端只在工作线程内使用，GUI 线程绝不直接调用
- GUI -> worker：cmd_q 队列传写命令
- worker -> GUI：data_q 队列传采样数据 / 错误信息 / 写入结果
- 断线后无需手动重连：同步客户端每次 execute() 前会自动 connect()
"""
import queue
import threading
import time

from pymodbus.client import ModbusTcpClient
from pymodbus.exceptions import (
    ModbusException,
    ConnectionException,
    ModbusIOException,
)

import regs_map as rm


class K10Worker:
    """独占 ModbusTcpClient 的采集/写入工作线程"""

    POLL_MS = 500          # 采样周期（ms），对应 2Hz 采样率
    DEVICE_ID = 1          # 从站地址 1

    def __init__(self, host: str, port: int = 502):
        self.host = host
        self.port = port
        self.cmd_q = queue.Queue()    # GUI -> worker：("write", {addr, values, name})
        self.data_q = queue.Queue()   # worker -> GUI：("data", hregs, iregs) / ("error", msg) / ("write_result", name, ok)
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True,
                                        name="modbus-worker")

    # ---------- 供 GUI 调用的接口 ----------
    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()

    def request_write(self, name: str, addr: int, values: list):
        """GUI 侧提交一次写请求（浮点参数请先用 client 编码，见 main.py 的 write_param）"""
        self.cmd_q.put(("write", {"name": name, "addr": addr, "values": values}))

    # ---------- 工作线程主体 ----------
    def _run(self):
        # 客户端只在本线程创建和使用
        client = ModbusTcpClient(self.host, port=self.port, timeout=2, retries=2)
        first = True

        while not self._stop.is_set():
            # 1) 优先处理 GUI 发来的写命令（每周期清空一次）
            try:
                while True:
                    kind, req = self.cmd_q.get_nowait()
                    if kind != "write":
                        continue
                    self._do_write(client, req)
            except queue.Empty:
                pass

            # 2) 周期采样：一次读全部保持寄存器 + 输入寄存器
            try:
                hr = client.read_holding_registers(0, count=rm.HOLD_COUNT,
                                                   device_id=self.DEVICE_ID)
                ir = client.read_input_registers(0, count=rm.INP_COUNT,
                                                 device_id=self.DEVICE_ID)
                if hr.isError() or ir.isError():
                    # 从站协议级错误（如非法地址），不抛异常，用 isError() 判断
                    self.data_q.put(("error", f"从站异常响应（可能地址非法）: "
                                              f"hr={hr.isError()} ir={ir.isError()}"))
                else:
                    if first:
                        print(f"[modbus] 已连接 {self.host}:{self.port}，开始采样")
                        first = False
                    self.data_q.put(("data", hr.registers, ir.registers))
            except (ConnectionException, ModbusIOException) as exc:
                # TCP 连接失败/超时/断开：显示明确错误信息，下个周期自动重连
                first = True
                self.data_q.put(("error", f"连接失败: {exc}"))
            except ModbusException as exc:
                self.data_q.put(("error", f"Modbus 错误: {exc}"))
            except Exception as exc:          # 兜底，防止意外异常杀死线程
                self.data_q.put(("error", f"意外错误: {exc!r}"))

            # 3) 按采样周期等待（可被 stop() 及时打断）
            self._stop.wait(self.POLL_MS / 1000.0)

        client.close()
        print("[modbus] 工作线程已退出")

    def _do_write(self, client, req):
        name, addr, values = req["name"], req["addr"], req["values"]
        try:
            wr = client.write_registers(addr, values, device_id=self.DEVICE_ID)
            if wr.isError():
                self.data_q.put(("write_result", name, False,
                                 f"从站拒绝写入（异常码 0x{wr.exception_code:02X}）"))
            else:
                self.data_q.put(("write_result", name, True, "写入成功"))
        except (ConnectionException, ModbusIOException) as exc:
            self.data_q.put(("write_result", name, False, f"写入失败: {exc}"))
        except ModbusException as exc:
            self.data_q.put(("write_result", name, False, f"写入失败: {exc}"))
        except Exception as exc:
            self.data_q.put(("write_result", name, False, f"意外错误: {exc!r}"))
