idf_component_register(SRCS "main.c" "xl9535.c" "aht20.c" "tank_model.c" "pid.c"
                            "wifi_sta.c" "modbus_slave.c" "ui.c" "ui_core.c"
                            "fonts/lv_font_cjk_18.c" "fonts/lv_font_cjk_20.c"
                            "fonts/lv_font_cjk_24b.c" "fonts/lv_font_cjk_28b.c"
                       INCLUDE_DIRS "." "fonts"
                       REQUIRES esp_driver_i2c esp_timer esp_wifi esp_netif
                                 esp_event nvs_flash esp_lcd esp_driver_spi
                                 lvgl esp_lvgl_port)
# 注：esp-modbus/esp_lcd_ili9341 在 idf_component.yml 中声明，组件管理器自动加入依赖
