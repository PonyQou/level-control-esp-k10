# 液位控制站项目实操指南（项目一 + 项目二）

> 适用读者：接手本项目的组员 / 需要复现实验的同学。
> 本指南覆盖：从零冷启动 → 完整验收演示 → 常见故障排查 → 日常维护。
> 前置说明：环境已在本机装好（ESP-IDF v6.1 于 D:\esp），以下步骤均为"已有环境"下的操作。

---

## 一、项目总览（30 秒看懂）

```
┌─ 项目一 ─────────────────────────────────────────────┐
│ K10 板子（ESP32-S3）                                 │
│   液位容器仿真 + PID 闭环 + LCD 界面 + A/B 按键        │
│   Modbus TCP 从站（端口 502，站号 1，走 WiFi）         │
│          ↕ Modbus TCP                                │
│ PC 上位机（Python：main.py）                         │
│   显示/改参数/记录 CSV/画曲线                         │
└──────────────────────────────────────────────────────┘
┌─ 项目二 ─────────────────────────────────────────────┐
│ PC 网关（Python：gateway.py）                        │
│   读 K10 的 Modbus → 翻译成 OPC UA（DI 信息模型）     │
│          ↕ OPC UA（端口 4840）                       │
│ 任何 OPC UA 客户端（UaExpert）                       │
│   浏览 Tank1 / 读写变量 / 调用方法                    │
└──────────────────────────────────────────────────────┘
```

**核心事实**：
- 固件代码在 `D:\esp\projects\k10_level_control\firmware`（开发/编译在这，**不要在仓库副本里改代码**）
- 仓库（桌面\液位控制）里的 `项目一/firmware` 是提交用副本，用 `sync_firmware.py` 同步
- 所有 Python 环境（上位机/网关）都是各目录下的 `.venv`

---

## 二、关键路径与入口（速查表）

| 干什么 | 在哪/怎么操作 |
|---|---|
| 编译烧录固件 | `D:\esp\idf_cmd.bat -p COM3 flash`（在固件目录下执行） |
| 看串口日志 | `D:\esp\idf_cmd.bat -p COM3 monitor` |
| 改 WiFi 密码 | `D:\esp\idf_cmd.bat menuconfig` → 液位控制站配置 → 重新 flash |
| 启动上位机 | `项目一/pc_master/.venv\Scripts\python main.py` |
| 启动网关 | `项目二/gateway/.venv\Scripts\python gateway.py <K10的IP>` |
| 模拟从站（无板子联调） | `项目一/pc_master/.venv\Scripts\python mock_slave.py` |
| 跑验收实验 | `项目一/pc_master/.venv\Scripts\python experiments.py <K10的IP>` |
| UI 模拟器断言 | `D:\esp\projects\ui_sim\build\ui_sim.exe 120` + `check_ui.py` |
| 同步固件到仓库 | `python sync_firmware.py`（在仓库根目录） |
| 打包提交 zip | `提交物/build_zips.py` |
| 报告转 PDF | 各实验报告目录下的 `build_pdf.py` |

---

## 三、项目一：从零到完整演示

### 3.1 硬件连接（最易错的环节）

1. **USB 线插 K10 屏幕上方那个 USB-C 口**（底部标 "USB IN" 的口只供电不传数据！）
2. 必须用**能传数据的线**（手机原装线最稳）；插上电脑应有提示音
3. 板子靠 USB 供电，插上即开机

### 3.2 烧录固件

```bat
cd /d D:\esp\projects\k10_level_control\firmware
D:\esp\idf_cmd.bat -p COM3 flash      :: COM3 换成设备管理器里的实际串口号
D:\esp\idf_cmd.bat -p COM3 monitor    :: 看启动日志
```

**烧不进去时**：按住板子 **BOOT** 键不放 → 点一下 **RST** → 松 BOOT → 重新 flash。

**启动日志体检**（逐行确认）：
```
XL9535 初始化完成（背光已点亮）
AHT20 初始化完成
Modbus TCP 从站已启动（端口 502，站号 1）
已连接: 192.168.xxx.xxx          ← 记住这个 IP
```

### 3.3 上位机联调

```bat
cd /d <仓库路径>\项目一\pc_master
.venv\Scripts\python main.py
```

- IP 填 K10 屏幕上的地址 → 点连接 → 黄灯亮
- 改"设定液位"→ 看板子液面爬升；点"开始记录"→ 出 CSV；"回放CSV曲线"看历史

### 3.4 按键与屏幕

- **A 键**：出水阀 +10%；**B 键**：−10%（范围 0~100%）
- 按 A 后观察：液位先降 → 进水阀自动开大（IN 数值上升）→ 液位拉回设定值（**闭环演示**）
- 状态灯：主站黄=上位机已连；已到位青绿=误差 ≤±2mm；断网时主站灯变红

### 3.5 验收实验（一键脚本）

```bat
cd 项目一\pc_master
.venv\Scripts\python experiments.py 192.168.105.39
```
自动跑：稳态基线 30s → 设定阶跃 250→350（150s）→ 出水阀扰动（150s）→ 越界拒绝测试。
数据存 `实验数据/*.csv`，曲线用 `plot_report.py` 生成。

---

## 四、项目二：网关与 UaExpert

### 4.1 启动网关

```bat
cd /d <仓库路径>\项目二\gateway
.venv\Scripts\python gateway.py 192.168.105.39
:: 参数1 = K10 的 IP（看板子屏幕）；UA 端口默认 4840
```

### 4.2 UaExpert 验收操作

1. 打开 UaExpert → `Server → Add` → **Advanced** 模式
2. Endpoint Url：`opc.tcp://127.0.0.1:4840`（网关在本机，用回环地址）
3. Security Policy：**None**；Authentication：**Anonymous**；其余默认
4. 左侧双击连接 → 展开 `Objects → Tank1`

**验收动作清单**（对照项目二验收标准）：
- 双击 `Level` / `Temperature` → 实时值在跳（通讯+转换 ✓）
- 右键 `Setpoint` → Write 300 → 板子液面开始上升（可写 Variable ✓）
- 右键 `SetSetpoint` → Call 填 250 → 方法调用 ✓（越界值返回 BadOutOfRange）
- 看 `ModbusConnected` / `PollCount` / `LastError`（状态诊断 ✓）
- 类型树里确认 `TankType` 挂在 DI 的 `DeviceType` 下（DI 模型 ✓）

### 4.3 断网自持演示（两个项目通用验收项）

关手机热点 → K10 屏幕照常更新（主站灯变红）→ 开热点 → 自动重连，无需人工干预。

---

## 五、无板子时的纯软件联调（新手练习）

```bat
:: 终端 1：模拟从站
cd 项目一\pc_master && .venv\Scripts\python mock_slave.py
:: 终端 2：上位机（IP 填 127.0.0.1）
cd 项目一\pc_master && .venv\Scripts\python main.py
:: 终端 3：网关连模拟从站
cd 项目二\gateway && .venv\Scripts\python gateway.py 127.0.0.1
:: UaExpert 连 opc.tcp://127.0.0.1:4840 —— 三件套全通
```

---

## 五·五、组员 fresh-clone 复现清单（10 分钟上手）

```bat
:: 1. clone 仓库到任意英文路径
git clone <仓库地址>

:: 2. 上位机环境
cd 项目一\pc_master
python -m venv .venv
.venv\Scripts\pip install -r requirements.txt    :: 国内可加 -i https://pypi.tuna.tsinghua.edu.cn/simple

:: 3. 网关环境
cd ..\..\项目二\gateway
python -m venv .venv
.venv\Scripts\pip install -r requirements.txt

:: 4. 固件（有板子的组员）：自装 ESP-IDF v6.1（纯英文路径）后
::    idf.py set-target esp32s3 → menuconfig 填自己的 WiFi → flash
::    把固件目录同步进仓库：python sync_firmware.py <你的固件目录>
::    （无板子：跳过本步，上位机/网关可先用 mock_slave.py 联调）

:: 5. UaExpert：unified-automation.com 免费注册账号后下载（官网强制注册）
```

> 依赖清单：上位机 = pymodbus + matplotlib；网关 = asyncua + pymodbus；
> 均已写入各自 requirements.txt。Python 建议 3.10+（本项目用 3.13 开发）。

---

## 六、常见问题排查表

| 现象 | 排查顺序 |
|---|---|
| 电脑认不到板子 | ① 插的屏幕上方口？② 换数据线 ③ BOOT+RST 进下载模式 |
| WiFi 一直重连 | 热点必须是 **2.4GHz**（K10 不支持 5G）；密码核对 |
| 上位机/网关连不上 | 电脑与 K10 同一局域网？（电脑也连同一热点）；校园网有 AP 隔离 |
| 按键没反应/数值跳回 | 检查是否有**网关进程在跑**（网关旧版会回写）——任务管理器关掉重开最新版 |
| 网关起不来（端口占用） | 旧网关进程没关：任务管理器结束 python.exe（gateway.py）再启 |
| UaExpert 连不上 | 网关进程在跑吗？端口填 4840 吗？127.0.0.1 吗？ |
| 屏幕显示 --.-- | 控制被停止了（Stop 方法/上位机停止）→ 点 Start |
| 编译报 MSYSTEM/路径错误 | 必须用 `D:\esp\idf_cmd.bat`（不要直接用 idf.py） |

---

## 七、日常维护流程（改代码后）

```
1. 固件改代码 → 在 D:\esp\projects\k10_level_control\firmware 改
   → idf_cmd.bat flash 烧录验证
2. 同步仓库：仓库根目录运行 python sync_firmware.py
3. 提交 git（先开 Watt Toolkit 的 GitHub 加速）：
   git add -A && git commit -m "说明" && git push
4. 重打提交 zip：提交物/build_zips.py
5. 报告更新后：实验报告/build_pdf.py 重出 PDF
```

**本机环境的三大坑（新手必读，勿再踩）**：
1. **中文路径**：ESP-IDF 工具链在含中文的路径下会崩（0xC0000409）——固件必须留在 D 盘英文路径
2. **cmd 批处理**：不能有中文注释（GBK 解析错乱）；执行无路径 exe 要加 `.\` 前缀
3. **GitHub 直连不通**：git push 前必须开 Watt Toolkit（Steam++）的 GitHub 加速

---

## 八、验收标准对照速查

| 项目一验收 | 验证方式 | 实测 |
|---|---|---|
| 独立运行模型+PID | 断网观察屏幕 | ✓ |
| 稳态误差 ≤±2mm | 上位机读误差 | ✓ 0.04mm |
| LCD 全部信息 | 屏幕逐项对照 | ✓ |
| A/B 键可靠调整 | 按键看 OUT ±10% | ✓ |
| Modbus 读写 | 上位机 | ✓ |
| 非法参数拒绝 | 实验 D | ✓ 4/4 |
| 主站显示/记录/曲线 | 上位机 | ✓ |

| 项目二验收 | 验证方式 | 实测 |
|---|---|---|
| 网关连 K10 稳定读取 | PollCount 递增 | ✓ |
| UA Client 连接 | UaExpert | ✓ |
| DI 模型（TankType↔DeviceType） | 类型树 | ✓ |
| Variable 读/写 | UaExpert Write | ✓ |
| Method 改设定值 | UaExpert Call | ✓ 液面实际响应 |
| 寄存器↔工程值转换 | 数值一致性 | ✓ |
| 通讯状态诊断 | 三个诊断变量 | ✓ |

---

## 九、目录索引

```
液位控制/
├── README.md                ← 本文件（GitHub 首页自动渲染）
├── 项目一/
│   ├── firmware/            固件副本（提交用；开发在 D 盘）
│   ├── pc_master/           上位机（main.py 入口）
│   ├── docs/                PID调参/UI工作流/硬件实操指南
│   └── 实验报告/实验报告.pdf
├── 项目二/
│   ├── gateway/             网关（gateway.py 入口 + DI NodeSet）
│   ├── docs/方案.md
│   └── 实验报告/实验报告.pdf
├── 参考资料/                调研纪要/原理图/esp-modbus示例
├── 提交物/                  三个源码 zip + build_zips.py
└── sync_firmware.py         固件同步脚本

D:\esp\
├── idf_cmd.bat              ESP-IDF 统一入口
├── v6.1\esp-idf             IDF v6.1 源码
├── projects\k10_level_control\firmware   固件开发目录
└── projects\ui_sim          UI 模拟器（布局断言）
```
