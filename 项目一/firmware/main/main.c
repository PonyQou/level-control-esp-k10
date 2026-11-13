/* K10 液位控制站固件 —— 主程序
 *
 * 任务结构（ESP32-S3 双核）：
 *   - 控制任务（核心0，优先级8）：20ms 定周期 —— 按键消抖/出水阀调节、
 *     PID（按 PID 周期执行）、液位模型推进、刷新 Modbus 输入寄存器
 *   - 传感器任务：1s 周期读 AHT20 温度（阻塞约 90ms，独立成任务）
 *   - Modbus 任务：处理主站写事件（参数校验/保存命令）
 *   - Modbus 驱动任务：esp-modbus 组件自建（内部 select 轮询）
 *   - WiFi 事件：系统事件循环处理（重连），断网不影响本地模型
 *
 * 里程碑：
 *   M0 编译烧录自检 → M1 外设（XL9535/AHT20）→ M2/M3 LVGL 界面（ui.c，未启用）
 *   → M4/M5 模型+PID → M6 Modbus 联调（当前代码已含 M1/M4/M5/M6 逻辑）
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "driver/i2c_master.h"

#include "k10_board.h"
#include "xl9535.h"
#include "aht20.h"
#include "tank_model.h"
#include "pid.h"
#include "regs.h"
#include "wifi_sta.h"
#include "modbus_slave.h"
#include "ui.h"

static const char *TAG = "main";

#define CONTROL_PERIOD_MS   20     /* 控制/仿真周期 */
#define TEMP_PERIOD_MS      1000   /* 温度采样周期 */
#define UI_PERIOD_MS        100    /* 界面刷新周期 */

static xl9535_t s_io;
static aht20_t  s_aht;
static tank_t   s_tank;
static pid_ctrl_t s_pid;
static volatile float s_temp_c = 25.0f;
static esp_netif_t *s_sta_netif = NULL;

/* ---------------- 控制任务 ---------------- */
static void control_task(void *arg)
{
    (void)arg;
    TickType_t last_tick = xTaskGetTickCount();
    int64_t last_pid_us = esp_timer_get_time();
    int debounce_a = 0, debounce_b = 0;
    bool armed_a = false, armed_b = false;   /* 先看到松开才武装按键检测 */

    ESP_LOGI(TAG, "控制任务启动（周期 %d ms）", CONTROL_PERIOD_MS);

    while (1) {
        vTaskDelayUntil(&last_tick, pdMS_TO_TICKS(CONTROL_PERIOD_MS));

        /* ---- 按键 A/B：出水阀开度 ±10% ----
         * 消抖：连续 3 次采样为按下；按住不重复触发（触发后计数置 100）。
         * "先松开才武装"：必须至少看到一次松开状态，才允许检测按下——
         * 修复 K10 上电初期 XL9535 输入寄存器短暂读 0 导致的假按键
         * （实测每次启动 A 键自动 +10% 的毛刺） */
        uint8_t xl_in0 = 0, xl_in1 = 0;
        xl9535_read_inputs(&s_io, &xl_in0, &xl_in1);
        static uint8_t last_in0 = 0xFF, last_in1 = 0xFF;
        if (xl_in0 != last_in0 || xl_in1 != last_in1) {
            ESP_LOGI(TAG, "XL9535 输入变化: port0=0x%02X port1=0x%02X", xl_in0, xl_in1);
            last_in0 = xl_in0;
            last_in1 = xl_in1;
        }
        bool ka = xl9535_key_a(&s_io);     /* 经实测：P02 */
        bool kb = xl9535_key_b(&s_io);     /* 经实测：P14 */
        if (!ka) armed_a = true;
        if (!kb) armed_b = true;
        debounce_a = ka ? debounce_a + 1 : 0;
        debounce_b = kb ? debounce_b + 1 : 0;

        if (armed_a && debounce_a == 3) {
            modbus_slave_lock();
            float v = regs_get_f32(g_hold_regs, REG_VALVE_OUT) + 10.0f;
            if (v > 100.0f) v = 100.0f;
            regs_set_f32(g_hold_regs, REG_VALVE_OUT, v);
            modbus_slave_unlock();
            ESP_LOGI(TAG, "A 键：出水阀开度 %.0f%%", v);
        } else if (armed_b && debounce_b == 3) {
            modbus_slave_lock();
            float v = regs_get_f32(g_hold_regs, REG_VALVE_OUT) - 10.0f;
            if (v < 0.0f) v = 0.0f;
            regs_set_f32(g_hold_regs, REG_VALVE_OUT, v);
            modbus_slave_unlock();
            ESP_LOGI(TAG, "B 键：出水阀开度 %.0f%%", v);
        }
        if (debounce_a >= 3) debounce_a = 100;   /* 按住不重复触发 */
        if (debounce_b >= 3) debounce_b = 100;

        /* ---- 参数从保持寄存器读取（锁内快照，防 float 对与主站写入撕裂） ---- */
        modbus_slave_lock();
        float sp       = regs_get_f32(g_hold_regs, REG_SP);
        float valve_out= regs_get_f32(g_hold_regs, REG_VALVE_OUT);
        float area     = regs_get_f32(g_hold_regs, REG_AREA_CM2);
        float height   = regs_get_f32(g_hold_regs, REG_HEIGHT_MM);
        float qin_max  = regs_get_f32(g_hold_regs, REG_QIN_MAX);
        float qout_max = regs_get_f32(g_hold_regs, REG_QOUT_MAX);
        float exp_n    = regs_get_f32(g_hold_regs, REG_EXP_N);
        float speed    = regs_get_f32(g_hold_regs, REG_SPEED);
        uint32_t pid_ms= g_hold_regs[REG_PID_MS];
        bool running   = g_hold_regs[REG_RUN] != 0;
        float kp = regs_get_f32(g_hold_regs, REG_KP);
        float ki = regs_get_f32(g_hold_regs, REG_KI);
        float kd = regs_get_f32(g_hold_regs, REG_KD);
        modbus_slave_unlock();

        /* 第二道防线（F9 修复）：esp-modbus "先写后验"存在 10~30ms 校验滞后窗口，
         * 非法/NaN 写入可能被快照读到并永久毒化模型。此处 isfinite + 钳位，
         * 保证喂给模型/PID 的参数永远合法（寄存器回滚由 modbus 任务负责） */
        if (!isfinite(sp))       sp = 250.0f;
        if (!isfinite(kp))       kp = 3.0f;
        if (!isfinite(ki))       ki = 0.05f;
        if (!isfinite(kd))       kd = 0.5f;
        if (!isfinite(area))     area = 200.0f;
        if (!isfinite(height))   height = 500.0f;
        if (!isfinite(qin_max))  qin_max = 8.0f;
        if (!isfinite(qout_max)) qout_max = 4.0f;
        if (!isfinite(exp_n))    exp_n = 0.5f;
        if (!isfinite(speed))    speed = 1.0f;
        if (!isfinite(valve_out)) valve_out = 30.0f;
        sp       = fminf(fmaxf(sp,       SP_MIN),      SP_MAX);
        kp       = fminf(fmaxf(kp,       KP_MIN),      KP_MAX);
        ki       = fminf(fmaxf(ki,       KP_MIN),      KP_MAX);
        kd       = fminf(fmaxf(kd,       KP_MIN),      KP_MAX);
        area     = fminf(fmaxf(area,     AREA_MIN),    AREA_MAX);
        height   = fminf(fmaxf(height,   HEIGHT_MIN),  HEIGHT_MAX);
        qin_max  = fminf(fmaxf(qin_max,  QMAX_MIN),    QMAX_MAX);
        qout_max = fminf(fmaxf(qout_max, QMAX_MIN),    QMAX_MAX);
        exp_n    = fminf(fmaxf(exp_n,    EXP_N_MIN),   EXP_N_MAX);
        speed    = fminf(fmaxf(speed,    SPEED_MIN),   SPEED_MAX);
        valve_out= fminf(fmaxf(valve_out, VALVE_MIN),  VALVE_MAX);

        /* 模型参数热更新 */
        s_tank.area_cm2 = area;
        s_tank.height_mm = height;
        s_tank.qin_max = qin_max;
        s_tank.qout_max = qout_max;
        s_tank.exp_n = exp_n;
        s_tank.sim_speed = speed;
        s_tank.valve_out_pct = valve_out;
        s_tank.running = running;
        s_pid.kp = kp; s_pid.ki = ki; s_pid.kd = kd;
        s_pid.period_ms = pid_ms;

        /* ---- PID：按 PID 周期执行 ---- */
        int64_t now_us = esp_timer_get_time();
        if (running && (now_us - last_pid_us) >= (int64_t)pid_ms * 1000) {
            float dt_s = (now_us - last_pid_us) / 1000000.0f;
            last_pid_us = now_us;
            float out = pid_update(&s_pid, sp, s_tank.level_mm, dt_s);
            s_tank.valve_in_pct = out;
        } else if (!running) {
            s_tank.valve_in_pct = 0.0f;
            s_tank.flow_in = 0.0f;      /* 停止时清零流量，避免显示陈旧值 */
            s_tank.flow_out = 0.0f;
            pid_reset(&s_pid);
            last_pid_us = now_us;
        }

        /* 主站积分清零命令（modbus 任务置位，本任务消费） */
        if (modbus_slave_take_clear_i()) {
            pid_reset(&s_pid);
            ESP_LOGI(TAG, "收到积分清零命令");
        }

        /* ---- 液位模型推进（20ms） ---- */
        tank_step(&s_tank, CONTROL_PERIOD_MS / 1000.0f);

        /* ---- 刷新 Modbus 输入寄存器（锁内） ---- */
        float err = sp - s_tank.level_mm;
        modbus_slave_lock();
        regs_set_f32(g_in_regs, IN_LEVEL, s_tank.level_mm);
        regs_set_f32(g_in_regs, IN_SP, sp);
        regs_set_f32(g_in_regs, IN_ERR, err);
        regs_set_f32(g_in_regs, IN_VALVE_IN, s_tank.valve_in_pct);
        regs_set_f32(g_in_regs, IN_QIN, s_tank.flow_in);
        regs_set_f32(g_in_regs, IN_QOUT, s_tank.flow_out);
        regs_set_f32(g_in_regs, IN_TEMP, s_temp_c);
        regs_set_f32(g_in_regs, IN_PID_P, s_pid.p_term);
        regs_set_f32(g_in_regs, IN_PID_I, s_pid.i_term);
        regs_set_f32(g_in_regs, IN_PID_D, s_pid.d_term);
        regs_set_f32(g_in_regs, IN_PID_OUT, s_pid.output);

        uint16_t status = 0;
        if (running)                       status |= STATUS_PID_RUN;
        if (running && fabsf(err) <= 2.0f) status |= STATUS_AT_SETPOINT;
        if (modbus_slave_master_active())  status |= STATUS_MASTER_CONN;
        if (wifi_sta_connected())          status |= STATUS_WIFI_OK;
        g_in_regs[IN_STATUS] = status;
        modbus_slave_unlock();

        /* ---- 刷新 LCD 界面（每 100ms） ---- */
        int64_t ui_now = esp_timer_get_time() / 1000;
        static int64_t last_ui_ms = 0;
        if (ui_now - last_ui_ms >= UI_PERIOD_MS) {
            last_ui_ms = ui_now;
            ui_snapshot_t snap = {
                .level_mm = s_tank.level_mm,
                .setpoint_mm = sp,
                .height_mm = height,
                .valve_in_pct = s_tank.valve_in_pct,
                .valve_out_pct = valve_out,
                .flow_in = s_tank.flow_in,
                .flow_out = s_tank.flow_out,
                .temp_c = s_temp_c,
                .status = status,
                .data_valid = running,     /* 契约无数据规则：停止时显示 --.-- */
            };
            if (s_sta_netif) {
                esp_netif_ip_info_t ipinfo;
                if (esp_netif_get_ip_info(s_sta_netif, &ipinfo) == ESP_OK) {
                    esp_ip4addr_ntoa(&ipinfo.ip, snap.ip_str, sizeof(snap.ip_str));
                } else {
                    strcpy(snap.ip_str, "---");
                }
            } else {
                strcpy(snap.ip_str, "---");
            }
            ui_update(&snap);
        }
    }
}

/* ---------------- 传感器任务（AHT20 每 1s） ---------------- */
static void sensor_task(void *arg)
{
    (void)arg;
    TickType_t last_tick = xTaskGetTickCount();
    while (1) {
        vTaskDelayUntil(&last_tick, pdMS_TO_TICKS(TEMP_PERIOD_MS));
        float t = 0.0f, h = 0.0f;
        if (aht20_read(&s_aht, &t, &h) == ESP_OK) {
            s_temp_c = t;
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=========== K10 液位控制站启动 ===========");

    /* 基础服务 */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* I2C 总线（GPIO47/48：XL9535 + AHT20 共用） */
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = K10_I2C_SCL,
        .sda_io_num = K10_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));
    ESP_ERROR_CHECK(xl9535_init(&s_io, i2c_bus));     /* 背光点亮 */
    ESP_ERROR_CHECK(aht20_init(&s_aht, i2c_bus));

    /* 模型与 PID 初始状态 */
    tank_init(&s_tank);
    pid_init(&s_pid, 3.0f, 0.05f, 0.5f, 200);

    /* WiFi（非阻塞）→ Modbus TCP 从站（绑定 STA netif） */
    s_sta_netif = wifi_sta_init();
    ESP_ERROR_CHECK(modbus_slave_init(s_sta_netif));

    /* LCD 界面（ILI9341 + LVGL，含背光已点亮） */
    ESP_ERROR_CHECK(ui_init());

    /* 任务创建 */
    xTaskCreatePinnedToCore(control_task, "control", 4096, NULL, 8, NULL, 0);
    xTaskCreatePinnedToCore(sensor_task, "sensor", 2048, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(modbus_slave_task, "modbus", 4096, NULL, 6, NULL, 0);

    ESP_LOGI(TAG, "系统就绪：本地模型与 PID 独立运行，断网不受影响");
    /* TODO(M2/M3): LVGL 界面初始化（ui.c），显示罐体/数值/状态灯/IP 地址 */
}
