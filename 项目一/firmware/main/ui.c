/* LCD 板级初始化（ESP-IDF 专属）。界面本身在 ui_core.c（与 PC 模拟器共用）
 *
 * 板级配置依据 ricklon/unihiker-k10 真机验证的契约（参考资料/调研纪要.md）：
 *   - 正面竖屏、USB-C 在上、A/B 键在下，逻辑坐标 240×320
 *   - 40MHz SPI、BGR 颜色序、RGB565 字节交换、Y 镜像
 *   - 2 个 240×40 局部缓冲（DMA）
 */
#include "ui.h"
#include "k10_board.h"
#include "fonts/lv_font_cjk.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_lvgl_port.h"
#include "driver/spi_master.h"
#include <string.h>

static const char *TAG = "ui";

#define UI_LCD_PIXEL_CLK_HZ   (40 * 1000 * 1000)
#define UI_BUF_LINES          40          /* 局部缓冲行数（240×40×2 字节） */

esp_err_t ui_init(void)
{
    /* SPI2：ILI9341（无 MISO，只写） */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = K10_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = K10_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = UI_BUF_LINES * 240 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = K10_LCD_DC,
        .cs_gpio_num = K10_LCD_CS,
        .pclk_hz = UI_LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                             &io_cfg, &io));

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = K10_LCD_RST,              /* -1：无独立复位脚 */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR, /* ILI9341 接线为 BGR */
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_cfg, &panel));
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_mirror(panel, false, true);       /* X 不镜像，Y 镜像 */
    esp_lcd_panel_disp_on_off(panel, true);

    /* LVGL 端口（自建 LVGL 任务） */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = UI_BUF_LINES * 240 * sizeof(uint16_t),
        .double_buffer = true,
        .hres = 240,
        .vres = 320,
        .monochrome = false,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = true },
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    /* lvgl_port_add_disp 返回 lv_display_t*（esp_lvgl_port 2.x 签名），非 esp_err_t */
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "lvgl_port_add_disp 失败");
        return ESP_FAIL;
    }

    lvgl_port_lock(0);

    /* 主题：深灰底（契约 #222222 由 ui_core 设置） */
    lv_theme_t *th = lv_theme_default_init(disp,
        lv_palette_main(LV_PALETTE_GREY),
        lv_palette_main(LV_PALETTE_GREY),
        LV_THEME_DEFAULT_DARK,
        &lv_font_cjk_18);
    lv_display_set_theme(disp, th);

    ui_core_create(lv_screen_active(), &lv_font_cjk_20);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "LCD 界面初始化完成（240x320 竖屏）");
    return ESP_OK;
}

void ui_update(const ui_snapshot_t *s)
{
    if (s == NULL) {
        return;
    }
    lvgl_port_lock(0);
    ui_core_update(s);
    lvgl_port_unlock();
}
