/* Modbus TCP 从站（esp-modbus v2 句柄式 API）
 * 端口 502、从站地址 1（CONFIG_FMB_TCP_UID_ENABLED=y 严格校验）
 * 寄存器数组：g_hold_regs（可写参数）/ g_in_regs（只读状态），
 * 由本模块的从站任务 + 控制任务共享，跨任务读写必须 mbc_slave_lock/unlock。
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "regs.h"

/* 寄存器存储（0 基址，float32 = 2 寄存器 ABCD 布局） */
extern uint16_t g_hold_regs[HOLD_REG_COUNT];
extern uint16_t g_in_regs[IN_REG_COUNT];

/** @brief 初始化从站：创建 + 注册寄存器区 + 启动（netif 传 wifi_sta_init 的返回值） */
esp_err_t modbus_slave_init(void *netif);

/** @brief 从站事件处理任务：校验主站写参数（越界恢复旧值）、
 *         处理保存/清零积分命令、刷新"主站已连接"时间戳 */
void modbus_slave_task(void *arg);

/** @brief 主站最近 5 秒内是否有访问（状态位 STATUS_MASTER_CONN 用；
 *         uint32 时间戳保证原子读，开机初期无访问时不误报） */
bool modbus_slave_master_active(void);

/** @brief 主站是否请求过积分清零（取走并清除标志，控制任务每周期轮询） */
bool modbus_slave_take_clear_i(void);

/** @brief 寄存器临界区锁（封装 mbc_slave_lock/unlock，其他任务读写寄存器必须成对使用） */
void modbus_slave_lock(void);
void modbus_slave_unlock(void);

/* ---- 寄存器读写助手（调用前自行加锁） ---- */
void  regs_set_f32(uint16_t *arr, int off, float v);
float regs_get_f32(const uint16_t *arr, int off);
