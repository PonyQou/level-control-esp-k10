/* WiFi STA 管理：非阻塞初始化 + 断线自动重连
 * 设计要点（esp-modbus 要求）：初始化只创建 netif 并启动连接，
 * 不阻塞等待拿到 IP —— Modbus TCP 从站绑定 netif 不依赖 IP。
 * 断线时仅 esp_wifi_connect() 重连，绝不销毁 netif（会连带销毁 Modbus）。
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif.h"

/** @brief 非阻塞启动 WiFi STA（SSID/密码来自 menuconfig）。
 *  @return WiFi STA 的 netif 指针（给 Modbus 从站绑定用） */
esp_netif_t *wifi_sta_init(void);

/** @brief 是否已拿到 IP（状态寄存器 STATUS_WIFI_OK 位用） */
bool wifi_sta_connected(void);
