# 项目一：基于嵌入式系统的液位控制站（K10 / ESP32-S3）

## ⚠️ 固件工程位置说明（重要）

**固件源码在 `D:\esp\projects\k10_level_control\firmware\`**，不在本目录下。

原因：ESP-IDF 的 CMake/ccache/ninja 工具链无法处理含中文的路径
（本机用户名为中文，桌面路径含"液位控制"）。固件移到了纯英文路径才能编译。

提交 zip 时从 `D:\esp\projects\k10_level_control\firmware` 打包（排除
build/、managed_components/、sdkconfig、dependencies.lock）。

## 项目结构

```
桌面/液位控制/项目一/
├── README.md            # 本文件
├── docs/                # 设计文档与实操指南
├── pc_master/           # PC 上位机（Python：pymodbus + tkinter + matplotlib）
└── 实验报告/            # 实验报告草稿

D:\esp\projects\k10_level_control\
└── firmware/            # ESP-IDF v6.1 固件工程（C，纯英文路径）
```

## 构建命令（Windows）

```bat
cd /d D:\esp\projects\k10_level_control\firmware
D:\esp\idf_cmd.bat build          :: 编译
D:\esp\idf_cmd.bat -p COMx flash  :: 烧录
D:\esp\idf_cmd.bat -p COMx monitor :: 串口日志
```

> idf_cmd.bat 已配置好 IDF 环境变量、国内镜像、UTF-8 支持；
> 从 Git Bash 调用时前缀 `chcp 65001 && `。

## 其余内容

系统架构、Modbus 寄存器表、液位模型、开发里程碑：见 docs/ 与
[调研纪要](../../参考资料/调研纪要.md)、[PID调参记录](docs/PID调参记录.md)、
[硬件实操指南](docs/硬件实操指南.md)。
