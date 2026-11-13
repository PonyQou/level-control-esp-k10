/* AHT20 驱动实现（datasheet 协议）
 * 校准:  0xBE 0x08 0x00（上电后一次，等待 ~40ms）
 * 测量:  0xAC 0x33 0x00（等待 80ms 后读 7 字节）
 * 温度 = raw20 * 200 / 2^20 - 50 (℃)；湿度 = raw20 * 100 / 2^20 (%)
 */
#include "aht20.h"
#include "k10_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "aht20";

esp_err_t aht20_init(aht20_t *s, i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = K10_AHT20_ADDR,
        .scl_speed_hz    = K10_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "添加 AHT20 设备失败: %s", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(50));                 /* 上电稳定 */

    uint8_t cmd[3] = { 0xBE, 0x08, 0x00 };         /* 校准命令 */
    err = i2c_master_transmit(s->dev, cmd, sizeof(cmd), 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AHT20 校准命令发送失败: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(40));                 /* 等待校准完成 */

    /* 校验校准位（状态字节 bit3 = 1 表示已校准） */
    uint8_t st = 0;
    if (i2c_master_transmit_receive(s->dev, (uint8_t[]){0x71}, 1, &st, 1, 100) == ESP_OK
        && !(st & 0x08)) {
        ESP_LOGW(TAG, "AHT20 校准位未置位（状态=0x%02X），继续尝试使用", st);
    }
    ESP_LOGI(TAG, "AHT20 初始化完成");
    return ESP_OK;
}

esp_err_t aht20_read(aht20_t *s, float *temp_c, float *hum_pct)
{
    uint8_t cmd[3] = { 0xAC, 0x33, 0x00 };         /* 触发测量 */
    esp_err_t err = i2c_master_transmit(s->dev, cmd, sizeof(cmd), 100);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(90));                 /* 测量耗时 ~80ms */

    uint8_t buf[7] = {0};
    err = i2c_master_receive(s->dev, buf, sizeof(buf), 100);
    if (err != ESP_OK) return err;

    if (buf[0] & 0x80) {                           /* 忙位 */
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t hum_raw = ((uint32_t)buf[1] << 12) | ((uint32_t)buf[2] << 4) | (buf[3] >> 4);
    uint32_t tmp_raw = ((uint32_t)(buf[3] & 0x0F) << 16) | ((uint32_t)buf[4] << 8) | buf[5];

    if (hum_pct) *hum_pct = hum_raw * 100.0f / 1048576.0f;
    if (temp_c)  *temp_c  = tmp_raw * 200.0f / 1048576.0f - 50.0f;
    return ESP_OK;
}
