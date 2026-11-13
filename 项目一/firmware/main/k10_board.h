/* K10 (DFR0992 行空板 K10) 板级引脚定义
 * 来源：官方原理图 + 官方 Arduino BSP (official_pins.h) + ricklon/unihiker-k10 真机验证
 * 详见 参考资料/调研纪要.md
 */
#pragma once

/* ---- I2C 共享总线（XL9535、AHT20、光照、加速度、音频ADC 共用） ---- */
#define K10_I2C_SCL       48
#define K10_I2C_SDA       47
#define K10_I2C_FREQ_HZ   100000

/* ---- LCD ILI9341 240x320（4线SPI，无 MISO，只写） ---- */
#define K10_LCD_SCLK      12
#define K10_LCD_MOSI      21
#define K10_LCD_CS        14
#define K10_LCD_DC        13
/* ILI9341 的 RST 未引出到 ESP32 GPIO（板载复位网络），esp_lcd 配置里填 -1 */
#define K10_LCD_RST       (-1)

/* ---- XL9535 IO 扩展（I2C 地址 0x20）上的资源 ----
 * 注意：原理图网络名 "P5/KeyA"、"P11/KeyB" 中的 P5/P11 是 micro:bit
 * 边缘连接器的引脚名，不是 XL9535 引脚号。经真机实测（按下 A/B 观察
 * XL9535 输入寄存器跳变）确认：A 键 = P02，B 键 = P14。 */
#define K10_XL9535_ADDR   0x20
#define K10_BACKLIGHT_PIN 0x00   /* P00：LCD 背光，高有效（实测验证） */
#define K10_KEYA_PIN      0x02   /* P02：按键 A，按下低电平（实测验证） */
#define K10_KEYB_PIN      0x14   /* P14：按键 B，按下低电平（实测验证） */

/* ---- AHT20 温湿度（I2C 地址 0x38），用作液体温度演示值 ---- */
#define K10_AHT20_ADDR    0x38

/* ---- WS2812 RGB x3 ---- */
#define K10_RGB_PIN       46

/* ---- 其他 ---- */
#define K10_BOOT_PIN      0      /* BOOT 键 = GPIO0 */
