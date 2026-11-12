# -*- coding: utf-8 -*-
"""液位控制站 PC 主站（Modbus TCP 主站）

功能：
- 连接 K10 的 Modbus TCP 从站（端口 502，从站地址 1）
- 显示运行状态（主站/已到位/WiFi/PID 指示灯）与实时数值
- 读写模型参数、PID 参数、设定液位、出水阀开度（客户端范围校验）
- 连续采集实验数据保存为 CSV（utf-8-sig，Excel 直接打开不乱码）
- 实时曲线（嵌入窗口，matplotlib FuncAnimation）与 CSV 回放曲线、保存 PNG
- 连接失败显示明确错误信息，自动重连

线程模型（pymodbus 客户端非线程安全）：
- 工作线程独占 ModbusTcpClient，负责轮询与写命令
- GUI 主线程只通过两个 queue 与工作线程通信，用 root.after() 消费数据
运行：python main.py
"""
import csv
import queue
import struct
import threading
import time
from collections import deque
from datetime import datetime

import tkinter as tk
from tkinter import ttk, filedialog, messagebox

import matplotlib
matplotlib.use("TkAgg")                     # 与 tkinter 同进程必须在 import pyplot 前设置
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.animation import FuncAnimation

import regs_map as rm
from modbus_io import K10Worker
from data_log import DataLogger

# matplotlib 中文支持
plt.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei"]
plt.rcParams["axes.unicode_minus"] = False

# ---------------- float32 编解码（与固件 ABCD 布局一致） ----------------
def f32_to_regs(v: float) -> list:
    """float -> [高字, 低字]（大端，高字在前）"""
    return list(struct.unpack(">HH", struct.pack(">f", v)))

def regs_to_f32(hi: int, lo: int) -> float:
    """[高字, 低字] -> float"""
    return struct.unpack(">f", struct.pack(">HH", hi, lo))[0]

def f32_from(regs: list, addr: int) -> float:
    return regs_to_f32(regs[addr], regs[addr + 1])


# ---------------- 灯（状态指示灯，Canvas 圆点） ----------------
LAMP_COLORS = {
    "off":  "#3a3f4a",
    "yellow": "#ffd54f",    # 主站已连
    "cyan":   "#4dd0e1",    # 已到位
    "green":  "#66bb6a",    # WiFi
    "blue":   "#64b5f6",    # PID 运行
}

class Lamp(tk.Canvas):
    def __init__(self, master, color, text):
        super().__init__(master, width=16, height=16, highlightthickness=0, bg="#1e2430")
        self._color = color
        self._oval = self.create_oval(3, 3, 13, 13, fill=LAMP_COLORS["off"], outline="")
        self._label = tk.Label(master, text=text, bg="#1e2430", fg="#c8d2e0",
                               font=("Microsoft YaHei", 9))
        self._label.pack(side="left", padx=(2, 10))

    def set(self, on: bool):
        self.itemconfig(self._oval, fill=self._color if on else LAMP_COLORS["off"])


# ---------------- 主窗口 ----------------
class App:
    BG = "#1e2430"        # 深蓝底色（与 K10 屏幕风格一致）
    FG = "#c8d2e0"

    def __init__(self, root):
        self.root = root
        root.title("液位控制站上位机 - Modbus TCP 主站")
        root.configure(bg=self.BG)
        root.protocol("WM_DELETE_WINDOW", self._on_close)

        self.worker = None
        self.logger = None          # DataLogger（记录中非 None）
        self.csv_path = None
        self.run_state = 0          # 运行状态（来自保持寄存器 RUN 的回读）
        self._closing = False       # 关闭标志：阻止 destroy 后的 after 回调继续执行
        self._after_id = None       # poll_q 的 after 定时器 id（关闭时取消）
        self.csv_var = tk.StringVar(value="未开始记录")

        # 实时曲线数据（10 分钟 @ 2Hz）
        self.t_data = deque(maxlen=1200)
        self.level_data = deque(maxlen=1200)
        self.sp_data = deque(maxlen=1200)
        self.t0 = time.time()

        self._build_ui()
        self._build_plot()

        self._poll_q()              # 启动队列消费循环

    # ================= UI 构建 =================
    def _build_ui(self):
        pad = {"padx": 6, "pady": 4}

        # ---- 连接栏 ----
        bar = tk.Frame(self.root, bg=self.BG)
        bar.pack(fill="x", **pad)
        tk.Label(bar, text="设备IP:", bg=self.BG, fg=self.FG).pack(side="left")
        self.ip_var = tk.StringVar(value="192.168.43.212")
        tk.Entry(bar, textvariable=self.ip_var, width=15).pack(side="left")
        tk.Label(bar, text=":502  从站1", bg=self.BG, fg="#8895a8").pack(side="left")
        self.conn_btn = ttk.Button(bar, text="连接", command=self._toggle_connect)
        self.conn_btn.pack(side="left", padx=8)

        # ---- 状态指示灯 ----
        lamps = tk.Frame(self.root, bg=self.BG)
        lamps.pack(fill="x", **pad)
        self.lamp_master = Lamp(lamps, LAMP_COLORS["yellow"], "主站已连")
        self.lamp_master.pack(side="left")
        self.lamp_wifi = Lamp(lamps, LAMP_COLORS["green"], "WiFi已连")
        self.lamp_wifi.pack(side="left")
        self.lamp_pid = Lamp(lamps, LAMP_COLORS["blue"], "PID运行")
        self.lamp_pid.pack(side="left")
        self.lamp_atsp = Lamp(lamps, LAMP_COLORS["cyan"], "已到位")
        self.lamp_atsp.pack(side="left")

        # ---- 实时数值 ----
        vals = tk.LabelFrame(self.root, text=" 实时数据 ", bg=self.BG, fg=self.FG)
        vals.pack(fill="x", **pad)
        self.var = {}     # name -> StringVar
        self.vals = [     # (显示名, 内部键, 单位, 格式)
            ("当前液位", "LEVEL", "mm", ".1f"),
            ("设定值",   "SP",    "mm", ".1f"),
            ("误差",     "ERR",   "mm", ".1f"),
            ("进水阀",   "VALVE_IN", "%", ".1f"),
            ("进水流量", "QIN",   "L/min", ".2f"),
            ("出水阀",   "VALVE_OUT", "%", ".1f"),
            ("出水流量", "QOUT",  "L/min", ".2f"),
            ("液体温度", "TEMP",  "℃", ".1f"),
            ("PID P/I/D", "PID_PID", "%", "s"),
            ("PID输出",  "PID_OUT", "%", ".1f"),
        ]
        for i, (label, key, unit, fmt) in enumerate(self.vals):
            r, c = divmod(i, 5)
            cell = tk.Frame(vals, bg=self.BG)
            cell.grid(row=r, column=c, sticky="w", padx=14, pady=4)
            tk.Label(cell, text=label, bg=self.BG, fg="#8895a8",
                     font=("Microsoft YaHei", 9)).pack(anchor="w")
            self.var[key] = tk.StringVar(value="--")
            tk.Label(cell, textvariable=self.var[key], bg=self.BG, fg="#8fd3ff",
                     font=("Consolas", 14, "bold")).pack(anchor="w")
            tk.Label(cell, text=unit, bg=self.BG, fg="#8895a8",
                     font=("Microsoft YaHei", 8)).pack(anchor="w")

        # ---- 参数读写 ----
        params = tk.LabelFrame(self.root, text=" 参数读写（写入前做范围校验） ", bg=self.BG, fg=self.FG)
        params.pack(fill="x", **pad)
        self.entry = {}
        self.params = [  # (内部键, 显示名, 单位, 是否整数)
            ("SP", "设定液位", "mm", False),
            ("Kp", "Kp", "", False),
            ("Ki", "Ki", "", False),
            ("Kd", "Kd", "", False),
            ("PID_MS", "PID周期", "ms", True),
            ("VALVE_OUT", "出水阀开度", "%", False),
            ("AREA_CM2", "容器面积", "cm²", False),
            ("HEIGHT_MM", "容器高度", "mm", False),
            ("QIN_MAX", "进水最大流量", "L/min", False),
            ("QOUT_MAX", "出水最大流量", "L/min", False),
            ("EXP_N", "出水指数", "", False),
            ("SPEED", "仿真速度", "×", False),
        ]
        for i, (key, label, unit, is_int) in enumerate(self.params):
            r, c = divmod(i, 6)
            cell = tk.Frame(params, bg=self.BG)
            cell.grid(row=r, column=c, sticky="w", padx=8, pady=3)
            tk.Label(cell, text=f"{label}({unit})" if unit else label,
                     bg=self.BG, fg="#8895a8", font=("Microsoft YaHei", 9)).pack(anchor="w")
            e = ttk.Entry(cell, width=12)
            e.pack(side="left")
            self.entry[key] = e
            ttk.Button(cell, text="写", width=3,
                       command=lambda k=key, n=label, ii=is_int: self._write_param(k, n, ii)
                       ).pack(side="left", padx=3)

        # ---- 控制按钮 ----
        ctrl = tk.Frame(self.root, bg=self.BG)
        ctrl.pack(fill="x", **pad)
        self.run_btn = ttk.Button(ctrl, text="启动控制", command=self._toggle_run)
        self.run_btn.pack(side="left", padx=4)
        ttk.Button(ctrl, text="保存参数到K10", command=lambda: self._write_u16("SAVE", 1)).pack(side="left", padx=4)
        ttk.Button(ctrl, text="PID积分清零", command=lambda: self._write_u16("CLEAR_I", 1)).pack(side="left", padx=4)

        # ---- 数据记录 ----
        rec = tk.LabelFrame(self.root, text=" 实验数据记录 ", bg=self.BG, fg=self.FG)
        rec.pack(fill="x", **pad)
        ttk.Button(rec, text="开始记录", command=self._start_log).pack(side="left", padx=4)
        ttk.Button(rec, text="停止记录", command=self._stop_log).pack(side="left", padx=4)
        ttk.Button(rec, text="回放CSV曲线", command=self._replay).pack(side="left", padx=4)
        ttk.Button(rec, text="保存当前曲线PNG", command=self._save_png).pack(side="left", padx=4)
        tk.Label(rec, textvariable=self.csv_var, bg=self.BG, fg="#8895a8",
                 font=("Microsoft YaHei", 9)).pack(side="left", padx=10)

        # ---- 消息栏 ----
        self.msg_var = tk.StringVar(value="未连接。请输入 K10 的 IP 后点击连接。")
        tk.Label(self.root, textvariable=self.msg_var, bg="#161b24", fg="#ffb74d",
                 anchor="w", font=("Microsoft YaHei", 9)).pack(fill="x", side="bottom")

    def _build_plot(self):
        """嵌入式实时曲线：液位（实线）+ 设定值（虚线）"""
        plotf = tk.LabelFrame(self.root, text=" 实时曲线 ", bg=self.BG, fg=self.FG)
        plotf.pack(fill="both", expand=True, **{"padx": 6, "pady": 4})
        self.fig, self.ax = plt.subplots(figsize=(8, 3), dpi=100)
        self.fig.patch.set_facecolor(self.BG)
        self.ax.set_facecolor("#161b24")
        self.ax.tick_params(colors=self.FG, labelsize=8)
        for s in self.ax.spines.values():
            s.set_color("#3a3f4a")
        self.line_level, = self.ax.plot([], [], lw=1.6, color="#8fd3ff", label="实际液位")
        self.line_sp, = self.ax.plot([], [], lw=1.2, color="#ffd54f",
                                     linestyle="--", label="设定液位")
        self.ax.set_xlabel("时间 (s)", color=self.FG)
        self.ax.set_ylabel("液位 (mm)", color=self.FG)
        self.ax.legend(facecolor=self.BG, labelcolor=self.FG, framealpha=0.3)
        self.canvas = FigureCanvasTkAgg(self.fig, master=plotf)
        self.canvas.get_tk_widget().pack(fill="both", expand=True)
        # 必须保存 ani 引用，否则被 GC 后动画停止
        self.ani = FuncAnimation(self.fig, self._plot_update, interval=500,
                                 blit=False, cache_frame_data=False)

    def _plot_update(self, _):
        if not self.t_data:
            return self.line_level, self.line_sp
        t = [x - self.t0 for x in self.t_data]
        self.line_level.set_data(t, self.level_data)
        self.line_sp.set_data(t, self.sp_data)
        self.ax.relim()
        self.ax.autoscale_view()
        self.ax.set_ylim(bottom=0)
        return self.line_level, self.line_sp

    # ================= 连接管理 =================
    def _toggle_connect(self):
        if self.worker is None:
            ip = self.ip_var.get().strip()
            if not ip:
                messagebox.showwarning("提示", "请先输入 K10 的 IP 地址")
                return
            self.worker = K10Worker(ip, port=502)
            self.worker.start()
            self.conn_btn.config(text="断开")
            self.msg_var.set(f"正在连接 {ip}:502 ...")
        else:
            self.worker.stop()
            self.worker = None
            self.conn_btn.config(text="连接")
            self.lamp_master.set(False)
            self.msg_var.set("已断开连接。")

    def _on_close(self):
        self._closing = True
        # 取消尚未执行的 poll_q 定时器，避免销毁后触发产生 Tcl 报错
        if self._after_id is not None:
            try:
                self.root.after_cancel(self._after_id)
            except Exception:
                pass
        # 停止 FuncAnimation 事件源，否则窗口销毁后它还会继续触发回调
        try:
            self.ani.event_source.stop()
        except Exception:
            pass
        if self.worker:
            self.worker.stop()
        if self.logger:
            self.logger.close()
        # 注意：嵌入 FigureCanvasTkAgg + FuncAnimation 时，直接 destroy() 无法退出
        # tkinter 事件循环（matplotlib 3.11 实测），必须先 quit() 再 destroy()
        self.root.quit()
        self.root.destroy()

    # ================= 队列消费（GUI 主线程，每 100ms） =================
    def _poll_q(self):
        if self._closing:
            return
        if self.worker:
            try:
                while True:
                    ev = self.worker.data_q.get_nowait()
                    kind = ev[0]
                    if kind == "data":
                        _, hregs, iregs = ev
                        self._on_data(hregs, iregs)
                    elif kind == "error":
                        self.lamp_master.set(False)
                        self.msg_var.set(ev[1])
                    elif kind == "write_result":
                        _, name, ok, detail = ev
                        self.msg_var.set(f"[{name}] {detail}")
            except queue.Empty:
                pass
        try:
            self._after_id = self.root.after(100, self._poll_q)
        except tk.TclError:
            pass   # 窗口已销毁，不再调度

    # ================= 数据处理 =================
    def _on_data(self, hregs, iregs):
        self.lamp_master.set(True)
        # 输入寄存器解码
        level = f32_from(iregs, rm.INP["LEVEL"])
        sp    = f32_from(iregs, rm.INP["SP"])
        err   = f32_from(iregs, rm.INP["ERR"])
        vin   = f32_from(iregs, rm.INP["VALVE_IN"])
        qin   = f32_from(iregs, rm.INP["QIN"])
        qout  = f32_from(iregs, rm.INP["QOUT"])
        temp  = f32_from(iregs, rm.INP["TEMP"])
        p_, i_, d_ = (f32_from(iregs, rm.INP["PID_P"]),
                      f32_from(iregs, rm.INP["PID_I"]),
                      f32_from(iregs, rm.INP["PID_D"]))
        pid_out = f32_from(iregs, rm.INP["PID_OUT"])
        status = iregs[rm.INP["STATUS"]]
        vout = f32_from(hregs, rm.HOLD["VALVE_OUT"])
        self.run_state = hregs[rm.HOLD["RUN"]]
        self.run_btn.config(text="停止控制" if self.run_state else "启动控制")

        # 刷新数值显示
        self.var["LEVEL"].set(f"{level:.1f}")
        self.var["SP"].set(f"{sp:.1f}")
        self.var["ERR"].set(f"{err:.1f}")
        self.var["VALVE_IN"].set(f"{vin:.1f}")
        self.var["QIN"].set(f"{qin:.2f}")
        self.var["VALVE_OUT"].set(f"{vout:.1f}")
        self.var["QOUT"].set(f"{qout:.2f}")
        self.var["TEMP"].set(f"{temp:.1f}")
        self.var["PID_PID"].set(f"{p_:.0f}/{i_:.0f}/{d_:.0f}")
        self.var["PID_OUT"].set(f"{pid_out:.1f}")

        # 指示灯（设备状态位）
        self.lamp_wifi.set(bool(status & rm.STATUS_BITS["WIFI_OK"]))
        self.lamp_pid.set(bool(status & rm.STATUS_BITS["PID_RUN"]))
        self.lamp_atsp.set(bool(status & rm.STATUS_BITS["AT_SETPOINT"]))

        # 实时曲线 + CSV
        now = time.time()
        self.t_data.append(now)
        self.level_data.append(level)
        self.sp_data.append(sp)
        if self.logger:
            self.logger.write(level, sp, err, qin, qout, vin, vout,
                              f32_from(hregs, rm.HOLD["Kp"]),
                              f32_from(hregs, rm.HOLD["Ki"]),
                              f32_from(hregs, rm.HOLD["Kd"]),
                              hregs[rm.HOLD["PID_MS"]],
                              temp, status)

    # ================= 写操作 =================
    def _write_param(self, key, label, is_int):
        if self.worker is None:
            messagebox.showwarning("提示", "请先连接设备")
            return
        txt = self.entry[key].get().strip()
        try:
            val = int(txt) if is_int else float(txt)
        except ValueError:
            messagebox.showerror("输入错误", f"[{label}] 请输入数字")
            return
        lo, hi = rm.RANGES[key]
        if not (lo <= val <= hi):
            messagebox.showerror("输入错误", f"[{label}] 超出范围 {lo}~{hi}")
            return
        if is_int:
            self.worker.request_write(key, rm.HOLD[key], [val])
        else:
            self.worker.request_write(key, rm.HOLD[key], f32_to_regs(val))

    def _write_u16(self, key, val):
        if self.worker is None:
            messagebox.showwarning("提示", "请先连接设备")
            return
        self.worker.request_write(key, rm.HOLD[key], [val])

    def _toggle_run(self):
        if self.worker is None:
            messagebox.showwarning("提示", "请先连接设备")
            return
        self.worker.request_write("RUN", rm.HOLD["RUN"],
                                  [1 if not self.run_state else 0])

    # ================= CSV 记录 =================
    def _start_log(self):
        if self.logger:
            return
        self.csv_path = f"实验数据_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        self.logger = DataLogger(self.csv_path)
        self.csv_var.set(f"记录中: {self.csv_path}")

    def _stop_log(self):
        if self.logger:
            self.logger.close()
            self.logger = None
            self.csv_var.set(f"已保存: {self.csv_path}")

    # ================= 曲线 =================
    def _save_png(self):
        path = filedialog.asksaveasfilename(defaultextension=".png",
                                            filetypes=[("PNG 图片", "*.png")],
                                            initialfile="实时曲线.png")
        if path:
            self.fig.savefig(path, dpi=150)
            self.msg_var.set(f"曲线已保存: {path}")

    def _replay(self):
        """读取 CSV 文件并绘制液位/设定值/误差曲线（可保存 PNG）"""
        path = filedialog.askopenfilename(filetypes=[("CSV 数据", "*.csv")])
        if not path:
            return
        try:
            with open(path, encoding="utf-8-sig", newline="") as f:
                rows = list(csv.DictReader(f))
        except OSError as e:
            messagebox.showerror("读取失败", f"无法打开 CSV 文件: {e}")
            return
        if not rows:
            messagebox.showwarning("提示", "CSV 文件为空")
            return

        t = [r["时间"][-8:] for r in rows]     # 只显示 时:分:秒
        level = [float(r["液位mm"]) for r in rows]
        sp = [float(r["设定值mm"]) for r in rows]
        err = [float(r["误差mm"]) for r in rows]

        top = tk.Toplevel(self.root)
        top.title(f"实验数据回放 - {path.split('/')[-1]}")
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6), sharex=True,
                                       gridspec_kw={"height_ratios": [3, 1]})
        ax1.plot(t, level, lw=1.4, label="实际液位")
        ax1.plot(t, sp, lw=1.1, linestyle="--", label="设定液位")
        ax1.set_ylabel("液位 (mm)")
        ax1.legend()
        ax1.grid(alpha=0.3)
        ax2.plot(t, err, lw=1.0, color="#ef5350", label="误差")
        ax2.axhline(2, color="gray", linestyle=":", lw=0.8)
        ax2.axhline(-2, color="gray", linestyle=":", lw=0.8)
        ax2.set_xlabel("时间")
        ax2.set_ylabel("误差 (mm)")
        ax2.legend()
        ax2.grid(alpha=0.3)
        step = max(1, len(t) // 10)
        ax1.set_xticks(t[::step])
        fig.autofmt_xdate(rotation=45)
        fig.tight_layout()

        canvas = FigureCanvasTkAgg(fig, master=top)
        canvas.get_tk_widget().pack(fill="both", expand=True)

        def save():
            p = filedialog.asksaveasfilename(defaultextension=".png",
                                             filetypes=[("PNG 图片", "*.png")],
                                             initialfile="回放曲线.png")
            if p:
                fig.savefig(p, dpi=150)
                self.msg_var.set(f"回放曲线已保存: {p}")

        ttk.Button(top, text="保存 PNG", command=save).pack(pady=4)


if __name__ == "__main__":
    root = tk.Tk()
    App(root)
    root.mainloop()
