# K10 液位控制站固件（ESP-IDF v6.1）

## 构建与烧录

```bat
:: 1. 激活 IDF 环境（EIM 安装后）
D:\esp\eim-cli.exe activate   :: 或使用 VS Code ESP-IDF 扩展自动完成

:: 2. 配置 WiFi（M6 阶段）：menuconfig 里填 WiFi 账号密码（占位符，真实密码不提交）
idf.py set-target esp32s3
idf.py menuconfig            :: 找到 Component config → 本工程 WiFi 配置

:: 3. 编译烧录（USB 线连接 K10；若识别不到，按住 BOOT 再点 RST 进入下载模式）
idf.py build
idf.py -p COMx flash monitor
```

## 目录规划（按里程碑添加模块）

```
main/
├── main.c           # app_main：任务创建与模块初始化（M0 自检 → 逐步扩展）
├── k10_board.h      # K10 引脚定义（M1）：I2C 47/48、LCD 12/21/14/13 等
├── xl9535.c/h       # M1 IO 扩展驱动：背光 P00、按键 A=P05 / B=P11
├── aht20.c/h        # M3 温度传感器 @0x38
├── tank_model.c/h   # M4 液位容器仿真（20ms 离散模型）
├── pid.c/h          # M5 PID（抗积分饱和）
├── ui.c/h           # M2/M3 LVGL 界面：罐体动画、数值、状态灯
├── wifi_sta.c/h     # M6 WiFi 连接与断线重连
├── modbus_slave.c/h # M6 Modbus TCP 从站（esp-modbus v2 API）
└── regs.h           # Modbus 寄存器映射（与上位机 regs_map.py 一致）
```

## 关键板级事实（详见 ../../参考资料/调研纪要.md）

- LCD ILI9341 240×320 SPI：SCLK=GPIO12 MOSI=GPIO21 CS=GPIO14 DC=GPIO13，无 MISO
- 背光 = XL9535 P00（高有效），按键 A=P05 / B=P11（按下低电平）
- I2C：SDA=GPIO47 SCL=GPIO48；AHT20 @0x38
- 16MB QIO Flash + 8MB Octal PSRAM（sdkconfig.defaults 已配置）

## 启用 LCD 界面（M2/M3 里程碑）

ui.c + fonts/ 已写好但暂未加入编译（先跑通无屏版本再启用）：

1. `main/CMakeLists.txt` 的 SRCS 加入 `"ui.c" "fonts/lv_font_cjk_16.c" "fonts/lv_font_cjk_24.c"`，
   REQUIRES 加入 `esp_lcd esp_driver_spi lvgl esp_lvgl_port`
2. `main/idf_component.yml` 加入 `lvgl/lvgl: "^9.5.0"` 和 `espressif/esp_lvgl_port: "^2.8.0"`
3. `main.c` 的 app_main 调 `ui_init()`，控制任务每 100ms 填 ui_snapshot_t 调 `ui_update()`

首次点亮必须用四边 TOP/BOTTOM/LEFT/RIGHT + 纯色块验收画面核对方向与颜色
（ricklon 契约：BGR、字节交换、Y 镜像——若不对只调 ui.c 的 panel/rotation 配置）。

## 提交 zip 排除

build/、managed_components/、sdkconfig（含真实 WiFi 密码）、dependencies.lock
