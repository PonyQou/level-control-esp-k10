# Modbus TCP → OPC UA 协议网关（项目二）

## 运行

```bat
cd 项目二\gateway
python -m venv .venv
.venv\Scripts\activate
pip install -i https://pypi.tuna.tsinghua.edu.cn/simple asyncua pymodbus
python gateway.py <K10_IP> <UA端口=4840>
```

## 架构

```
K10 (Modbus TCP 从站 :502)  ←→  网关（本项目，PC 运行）  ←→  第三方 UA Client
                               ├ asyncua 服务器 (opc.tcp://*:4840)
                               ├ 官方 DI NodeSet（nodesets/Opc.Ua.Di.NodeSet2.xml）
                               ├ TankType（DeviceType 子类型）+ Tank1 实例
                               ├ 变量 18 个（液位/流量/阀门/PID/模型参数/诊断）
                               ├ 方法 6 个（SetSetpoint/SetValveOut/SaveParams/
                               │         ClearIntegral/Start/Stop）
                               └ 双向转换：寄存器 uint16 对 ↔ 工程 float（ABCD）
```

## 关键实现记录（迭代中解决的问题，报告素材）

1. **DI NodeSet 导入**：asyncua 的 XML 导入器 strict 模式无法处理 DI 1.0
   废弃参数节点的断链 → `strict_mode=False` 导入 438 节点，DeviceType
   （ns;i=1002）层级完整
2. **类型实例化**：asyncua 的 `add_object` 不会自动复制类型子节点
   （实测实例子节点数=0）→ 变量/方法在类型（建模声明）与实例（运行）
   双挂，满足浏览与调用
3. **BadTypeMismatch 三连**：
   - `Variant(0, Boolean)` 初值不是 bool → 按类型给初值
   - Python int 写 UInt16/UInt32 变量不自动转换 → 显式 `ua.Variant(int, vtype)`
   - `ua.StatusCodes.Bad_OutOfRange` 不存在 → 正确名 `BadOutOfRange`
4. **初始化竞态**：UA 变量初值 0 ≠ Modbus 实际值，差分写回逻辑把初值
   误判为客户端写入（开机把 0 写进设备）→ 首个成功轮询周期先回填
5. **写回竞态**：方法写 Modbus 后，差分逻辑把 UA 旧值又写回去
   （ValveOut 方法失效）→ 网关写 Modbus 后同步 UA 变量
6. **pymodbus 异步客户端**：必须在事件循环内创建；read 的 `count`/
   `device_id` 是 keyword-only 参数

## 验证

- G1~G4 对 mock 从站（项目一 pc_master/mock_slave.py）全功能测试通过
- G5 真机联调：读取全部真机数据 ✓、方法改设定值 → 液位爬升 ✓、
  越界方法返回 BadOutOfRange ✓、越界 UA 写恢复原值 + LastError ✓

## 验收对照

| 验收项 | 状态 |
|---|---|
| Modbus 通讯：连接 K10 稳定读取 | ✓（500ms 轮询，PollCount 递增） |
| OPC UA 服务：Client 能连接 | ✓（asyncua 客户端已验证，UaExpert 待实测） |
| DI 信息模型：TankType 基于 DeviceType | ✓（HasSubtype 关系已建立） |
| Variable 浏览/读/写 | ✓ |
| Method 修改设定值 | ✓ |
| 数据转换：寄存器 ↔ 工程值 | ✓（ABCD float32） |
| 状态诊断 | ✓（ModbusConnected/LastError/PollCount） |
