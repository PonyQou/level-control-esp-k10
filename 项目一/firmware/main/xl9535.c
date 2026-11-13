/* XL9535 驱动实现。
 * 寄存器（每端口一组，Port0=P00~P07，Port1=P10~P17）：
 *   0x00/0x01 输入端口  0x02/0x03 输出端口
 *   0x04/0x05 极性反转  0x06/0x07 方向配置（1=输入，0=输出）
 */
#include "xl9535.h"
#include "k10_board.h"
#include "esp_log.h"

static const char *TAG = "xl9535";

#define XL_REG_INPUT0   0x00
#define XL_REG_INPUT1   0x01
#define XL_REG_OUTPUT0  0x02
#define XL_REG_OUTPUT1  0x03
#define XL_REG_CONFIG0  0x06
#define XL_REG_CONFIG1  0x07

static esp_err_t write_reg(xl9535_t *io, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(io->dev, buf, sizeof(buf), 100);
}

static esp_err_t read_reg(xl9535_t *io, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(io->dev, &reg, 1, val, 1, 100);
}

esp_err_t xl9535_init(xl9535_t *io, i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = K10_XL9535_ADDR,
        .scl_speed_hz    = K10_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &io->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "添加 XL9535 设备失败: %s", esp_err_to_name(err));
        return err;
    }

    /* Port0：P00=背光输出(0)，P05=KeyA 输入(1)，其余输入 → 0xFE
     * Port1：P11=KeyB 输入(1)，其余输入 → 0xFF */
    err = write_reg(io, XL_REG_CONFIG0, 0xFE);
    if (err == ESP_OK) err = write_reg(io, XL_REG_CONFIG1, 0xFF);
    /* 背光点亮（P00 高），其余输出 0 */
    if (err == ESP_OK) err = write_reg(io, XL_REG_OUTPUT0, 0x01);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "XL9535 初始化失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "XL9535 初始化完成（背光已点亮）");
    return ESP_OK;
}

esp_err_t xl9535_backlight(xl9535_t *io, bool on)
{
    uint8_t out;
    esp_err_t err = read_reg(io, XL_REG_OUTPUT0, &out);
    if (err != ESP_OK) return err;
    uint8_t pin = K10_BACKLIGHT_PIN;    /* P00 → bit0 */
    out = on ? (out | (1u << pin)) : (out & ~(1u << pin));
    return write_reg(io, XL_REG_OUTPUT0, out);
}

bool xl9535_key_a(xl9535_t *io)
{
    uint8_t in;
    if (read_reg(io, XL_REG_INPUT0, &in) != ESP_OK) return false;
    return !(in & (1u << (K10_KEYA_PIN & 0x07)));   /* 按下为低 */
}

bool xl9535_key_b(xl9535_t *io)
{
    uint8_t in;
    if (read_reg(io, XL_REG_INPUT1, &in) != ESP_OK) return false;
    return !(in & (1u << (K10_KEYB_PIN & 0x07)));
}

esp_err_t xl9535_read_inputs(xl9535_t *io, uint8_t *port0, uint8_t *port1)
{
    esp_err_t err = read_reg(io, XL_REG_INPUT0, port0);
    if (err == ESP_OK) {
        err = read_reg(io, XL_REG_INPUT1, port1);
    }
    return err;
}
