# PC 主站（Modbus TCP 主站）

## 环境

- Python 3.13（Windows）
- 依赖：`pymodbus 3.16.2`、`matplotlib 3.11.2`（tkinter 为标准库）

```bat
python -m venv .venv
.venv\Scripts\activate
pip install -i https://pypi.tuna.tsinghua.edu.cn/simple pymodbus matplotlib
```

## 运行

```bat
.venv\Scripts\activate
python main.py
```

- 输入 K10 的 IP（设备屏幕上有显示），点击【连接】
- 没有硬件时可用模拟从站联调：先开一个终端 `python mock_slave.py`，
  上位机 IP 填 `127.0.0.1` 即可体验完整数据流（含 CSV 记录与曲线）

## 文件说明

| 文件 | 作用 |
|---|---|
| `main.py` | GUI 主程序：状态灯、实时数值、参数读写、CSV 记录、实时/回放曲线 |
| `modbus_io.py` | 轮询工作线程（独占 ModbusTcpClient，非线程安全约束下的正确用法） |
| `regs_map.py` | Modbus 寄存器映射表（与固件 `main/regs.h` 一一对应） |
| `data_log.py` | CSV 记录器（utf-8-sig + 每行 flush） |
| `mock_slave.py` | K10 模拟从站（开发联调用；控制器为 P-only 简化，稳态有约 3mm 残差属正常，写入参数会按范围钳位而不是拒绝） |

## 关键约定（与固件一致）

- 端口 502、从站地址 1（pymodbus ≥3.10 参数名为 `device_id=`）
- 所有 FLOAT32 占 2 个寄存器，ABCD 布局（高字在前，大端）
- 地址为 0 基址（4xxxx = 地址 + 40001），详见 `regs_map.py` 注释

## 提交 zip 前记得删除

`.venv/`、`__pycache__/`（课程要求）
