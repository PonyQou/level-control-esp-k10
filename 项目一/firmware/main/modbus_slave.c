/* Modbus TCP 从站实现（esp-modbus v2.1.4，句柄式 API，参考官方
 * examples/tcp/mb_tcp_slave —— IDF v6 已删除 v1 旧 API 和旧示例）
 *
 * 要点：
 * - 组件自建驱动任务做轮询，应用不需要 eMBPoll
 * - 应用用 mbc_slave_get_param_info(handle, &info, 超时) 非阻塞取读写事件，
 *   绝不用无超时的 check_event（会无限阻塞）
 * - 主站写寄存器时数组先被写入、后投递事件：校验失败需用镜像恢复旧值
 *   注：写事件是驱动任务写完寄存器后异步投递的，主站回读可能与"校验-回滚"
 *   并发（约 10~30ms 窗口内可能读到越界值），这是 esp-modbus 的固有行为
 * - 保存参数 = 把保持寄存器前段存 NVS（blob），上电自动恢复
 * - 线圈 0 与 REG_RUN 单向同步（主站写线圈 0 → 同步到保持寄存器）
 */
#include <string.h>
#include <math.h>
#include "modbus_slave.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mbcontroller.h"      /* esp-modbus v2 API */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "modbus_slave";

#define MASTER_CONN_TOUT_MS 5000   /* 主站活动超时（状态位"主站已连"） */

/* 寄存器存储 */
uint16_t g_hold_regs[HOLD_REG_COUNT];
uint16_t g_in_regs[IN_REG_COUNT];

static void *s_slave_handle = NULL;
static uint16_t s_hold_mirror[HOLD_REG_COUNT];   /* 写校验用旧值镜像 */
static uint8_t s_coils[2];                       /* 线圈区（16 个线圈，线圈0=运行） */
/* 32 位时间戳：Xtensa 上 uint32 的 volatile 读写是原子的（int64 会撕裂） */
static volatile uint32_t s_last_activity_ms = 0;
static volatile bool s_has_activity = false;     /* 防开机初期误报"主站已连" */
static volatile bool s_clear_i_requested = false;/* 主站请求积分清零 */

static const char *NVS_NS = "tank";
static const char *NVS_KEY = "params";           /* 保存前 23 个寄存器（到出水阀开度） */
#define PARAMS_SAVE_REGS 23

void regs_set_f32(uint16_t *arr, int off, float v)
{
    uint32_t u;
    memcpy(&u, &v, sizeof(u));                   /* 大端：高字在前 = ABCD */
    arr[off]     = (uint16_t)(u >> 16);
    arr[off + 1] = (uint16_t)(u & 0xFFFF);
}

float regs_get_f32(const uint16_t *arr, int off)
{
    uint32_t u = ((uint32_t)arr[off] << 16) | arr[off + 1];
    float v;
    memcpy(&v, &u, sizeof(v));
    return v;
}

/* ---- 参数校验：返回 false 表示该写入区域非法 ----
 * 所有 float 校验必须带 isfinite()：NaN 与任何数比较均为 false，
 * 能绕过范围校验并永久毒化模型 */
static bool validate_write(mb_param_info_t *info)
{
    int off = info->mb_offset;
    int n = (int)info->size;                     /* 组件文档：size 是寄存器个数（不是字节！）
                                                   曾误用 >>1 导致单寄存器写绕过全部校验 */

    /* 按起始偏移判断是哪个参数（每个参数占 1~2 个寄存器，校验覆盖区域内全部） */
    for (int i = 0; i < n; i++) {
        int r = off + i;
        if (r >= HOLD_REG_COUNT) return false;

        switch (r) {
        case REG_SP: case REG_SP + 1: {          /* 设定液位 mm */
            float v = regs_get_f32(g_hold_regs, REG_SP);
            if (!isfinite(v) || v < SP_MIN || v > SP_MAX) return false;
            break;
        }
        case REG_KP: case REG_KP + 1: {
            float v = regs_get_f32(g_hold_regs, REG_KP);
            if (!isfinite(v) || v < KP_MIN || v > KP_MAX) return false;
            break;
        }
        case REG_KI: case REG_KI + 1: {
            float v = regs_get_f32(g_hold_regs, REG_KI);
            if (!isfinite(v) || v < KP_MIN || v > KP_MAX) return false;
            break;
        }
        case REG_KD: case REG_KD + 1: {
            float v = regs_get_f32(g_hold_regs, REG_KD);
            if (!isfinite(v) || v < KP_MIN || v > KP_MAX) return false;
            break;
        }
        case REG_PID_MS:
            if (g_hold_regs[REG_PID_MS] < PID_MS_MIN
                || g_hold_regs[REG_PID_MS] > PID_MS_MAX) return false;
            break;
        case REG_AREA_CM2: case REG_AREA_CM2 + 1: {
            float v = regs_get_f32(g_hold_regs, REG_AREA_CM2);
            if (!isfinite(v) || v < AREA_MIN || v > AREA_MAX) return false;
            break;
        }
        case REG_HEIGHT_MM: case REG_HEIGHT_MM + 1: {
            float v = regs_get_f32(g_hold_regs, REG_HEIGHT_MM);
            if (!isfinite(v) || v < HEIGHT_MIN || v > HEIGHT_MAX) return false;
            break;
        }
        case REG_QIN_MAX: case REG_QIN_MAX + 1: {
            float v = regs_get_f32(g_hold_regs, REG_QIN_MAX);
            if (!isfinite(v) || v < QMAX_MIN || v > QMAX_MAX) return false;
            break;
        }
        case REG_QOUT_MAX: case REG_QOUT_MAX + 1: {
            float v = regs_get_f32(g_hold_regs, REG_QOUT_MAX);
            if (!isfinite(v) || v < QMAX_MIN || v > QMAX_MAX) return false;
            break;
        }
        case REG_EXP_N: case REG_EXP_N + 1: {
            float v = regs_get_f32(g_hold_regs, REG_EXP_N);
            if (!isfinite(v) || v < EXP_N_MIN || v > EXP_N_MAX) return false;
            break;
        }
        case REG_SPEED: case REG_SPEED + 1: {
            float v = regs_get_f32(g_hold_regs, REG_SPEED);
            if (!isfinite(v) || v < SPEED_MIN || v > SPEED_MAX) return false;
            break;
        }
        case REG_VALVE_OUT: case REG_VALVE_OUT + 1: {
            float v = regs_get_f32(g_hold_regs, REG_VALVE_OUT);
            if (!isfinite(v) || v < VALVE_MIN || v > VALVE_MAX) return false;
            break;
        }
        case REG_RUN:
            if (g_hold_regs[REG_RUN] > 1) return false;
            break;
        case REG_SAVE: case REG_CLEAR_I:          /* 命令寄存器，任意值合法 */
            break;
        default:
            return false;                        /* 未定义地址 */
        }
    }
    return true;
}

/* ---- NVS 参数保存/恢复 ---- */
static esp_err_t params_save(void)
{
    /* 锁内快照，防止与控制任务的寄存器刷新交错 */
    modbus_slave_lock();
    uint16_t snapshot[PARAMS_SAVE_REGS];
    memcpy(snapshot, g_hold_regs, sizeof(snapshot));
    modbus_slave_unlock();

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 打开失败: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_blob(h, NVS_KEY, snapshot, sizeof(snapshot));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "参数保存失败: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "参数已保存到 NVS");
    }
    return err;
}

static void params_load(void)
{
    nvs_handle_t h = 0;                    /* 必须初始化，nvs_open 失败时不 close */
    size_t len = PARAMS_SAVE_REGS * 2;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, NVS_KEY, g_hold_regs, &len) == ESP_OK
            && len == PARAMS_SAVE_REGS * 2) {
            ESP_LOGI(TAG, "已从 NVS 恢复上次保存的参数");
        }
        nvs_close(h);
    }
    memcpy(s_hold_mirror, g_hold_regs, sizeof(g_hold_regs));
}

/* 主站最近 TOUT 毫秒内是否有访问（uint32 减法天然防回绕） */
bool modbus_slave_master_active(void)
{
    if (!s_has_activity) {
        return false;
    }
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    return (now - s_last_activity_ms) < MASTER_CONN_TOUT_MS;
}

/* 主站是否请求过积分清零（取走并清除标志） */
bool modbus_slave_take_clear_i(void)
{
    bool req = s_clear_i_requested;
    s_clear_i_requested = false;
    return req;
}

void modbus_slave_lock(void)
{
    if (s_slave_handle) {
        mbc_slave_lock(s_slave_handle);
    }
}

void modbus_slave_unlock(void)
{
    if (s_slave_handle) {
        mbc_slave_unlock(s_slave_handle);
    }
}

esp_err_t modbus_slave_init(void *netif)
{
    /* 设置寄存器初始值（默认参数，与上位机 mock 一致） */
    memset(g_hold_regs, 0, sizeof(g_hold_regs));
    memset(g_in_regs, 0, sizeof(g_in_regs));
    regs_set_f32(g_hold_regs, REG_SP, 250.0f);
    regs_set_f32(g_hold_regs, REG_KP, 3.0f);
    regs_set_f32(g_hold_regs, REG_KI, 0.05f);
    regs_set_f32(g_hold_regs, REG_KD, 0.5f);
    g_hold_regs[REG_PID_MS] = 200;
    regs_set_f32(g_hold_regs, REG_AREA_CM2, 200.0f);
    regs_set_f32(g_hold_regs, REG_HEIGHT_MM, 500.0f);
    regs_set_f32(g_hold_regs, REG_QIN_MAX, 8.0f);
    regs_set_f32(g_hold_regs, REG_QOUT_MAX, 4.0f);
    regs_set_f32(g_hold_regs, REG_EXP_N, 0.5f);
    regs_set_f32(g_hold_regs, REG_SPEED, 1.0f);
    regs_set_f32(g_hold_regs, REG_VALVE_OUT, 30.0f);
    g_hold_regs[REG_RUN] = 1;
    s_coils[0] = 0x01;
    params_load();

    mb_communication_info_t comm = {
        .tcp_opts.port = CONFIG_FMB_TCP_PORT_DEFAULT,    /* 502 */
        .tcp_opts.mode = MB_TCP,
        .tcp_opts.addr_type = MB_IPV4,
        .tcp_opts.ip_addr_table = NULL,                  /* 绑定任意地址 */
        .tcp_opts.ip_netif_ptr = netif,                  /* WiFi STA netif */
        .tcp_opts.uid = 1,                               /* 从站地址 1 */
    };

    esp_err_t err = mbc_slave_create_tcp(&comm, &s_slave_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_create_tcp 失败: %s", esp_err_to_name(err));
        return err;
    }

    /* 保持寄存器区（可写参数） */
    mb_register_area_descriptor_t area = {
        .type = MB_PARAM_HOLDING,
        .start_offset = 0,
        .address = (void *)g_hold_regs,
        .size = sizeof(g_hold_regs),                     /* 字节数 = 寄存器数<<1 */
        .access = MB_ACCESS_RW,
    };
    err = mbc_slave_set_descriptor(s_slave_handle, area);
    if (err != ESP_OK) return err;

    /* 输入寄存器区（只读状态，应用周期性刷新） */
    area.type = MB_PARAM_INPUT;
    area.address = (void *)g_in_regs;
    area.size = sizeof(g_in_regs);
    err = mbc_slave_set_descriptor(s_slave_handle, area);
    if (err != ESP_OK) return err;

    /* 线圈区（线圈 0 与 REG_RUN 单向同步） */
    area.type = MB_PARAM_COIL;
    area.start_offset = 0;
    area.address = (void *)s_coils;
    area.size = sizeof(s_coils);
    err = mbc_slave_set_descriptor(s_slave_handle, area);
    if (err != ESP_OK) return err;

    err = mbc_slave_start(s_slave_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_start 失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Modbus TCP 从站已启动（端口 502，站号 1）");
    return ESP_OK;
}

void modbus_slave_task(void *arg)
{
    mb_param_info_t info = {0};

    while (1) {
        /* 非阻塞取主站访问事件（10ms 超时） */
        esp_err_t err = mbc_slave_get_param_info(s_slave_handle, &info, 10);
        if (err == ESP_ERR_TIMEOUT) {
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "get_param_info 错误: %s", esp_err_to_name(err));
            continue;
        }

        s_last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);
        s_has_activity = true;

        if (info.type & MB_EVENT_HOLDING_REG_WR) {
            /* 主站写了保持寄存器：校验，越界恢复旧值 */
            mbc_slave_lock(s_slave_handle);
            if (validate_write(&info)) {
                memcpy(s_hold_mirror, g_hold_regs, sizeof(g_hold_regs));
                ESP_LOGI(TAG, "主站写入 offset=%u 已接受", (unsigned)info.mb_offset);
            } else {
                memcpy(g_hold_regs, s_hold_mirror, sizeof(g_hold_regs));
                ESP_LOGW(TAG, "主站写入 offset=%u 越界，已拒绝并恢复原值",
                         (unsigned)info.mb_offset);
            }
            mbc_slave_unlock(s_slave_handle);

            /* 命令寄存器处理 */
            modbus_slave_lock();
            uint16_t save_cmd = g_hold_regs[REG_SAVE];
            uint16_t clear_cmd = g_hold_regs[REG_CLEAR_I];
            modbus_slave_unlock();

            if (save_cmd) {
                params_save();
                modbus_slave_lock();
                g_hold_regs[REG_SAVE] = 0;     /* 无论成败都清标志，避免死循环重试 */
                modbus_slave_unlock();
            }
            if (clear_cmd) {
                s_clear_i_requested = true;    /* 控制任务轮询此标志并清零积分 */
                modbus_slave_lock();
                g_hold_regs[REG_CLEAR_I] = 0;
                modbus_slave_unlock();
            }
        } else if (info.type & MB_EVENT_COILS_WR) {
            /* 线圈 0 单向同步到 REG_RUN（上位机运行/停止用保持寄存器） */
            modbus_slave_lock();
            g_hold_regs[REG_RUN] = (s_coils[0] & 0x01) ? 1 : 0;
            modbus_slave_unlock();
            ESP_LOGI(TAG, "线圈写入：运行=%u", (unsigned)g_hold_regs[REG_RUN]);
        }
    }
}
