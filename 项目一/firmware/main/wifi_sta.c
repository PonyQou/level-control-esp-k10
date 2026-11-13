#include "wifi_sta.h"
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

static const char *TAG = "wifi_sta";
static volatile bool s_connected = false;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();                      /* 启动后发起连接 */
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        ESP_LOGW(TAG, "WiFi 断开，自动重连...");
        esp_wifi_connect();                      /* 断线重连（不销毁 netif） */
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        s_connected = true;
        ESP_LOGI(TAG, "已连接: " IPSTR, IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG, "Modbus TCP 从站地址: " IPSTR ":502 (站号 1)",
                 IP2STR(&evt->ip_info.ip));
    }
}

esp_netif_t *wifi_sta_init(void)
{
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               event_handler, NULL));

    wifi_config_t wc = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* 必须关闭省电：否则 Modbus 轮询延迟大、易被 AP 断开（官方示例同款操作） */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "WiFi STA 已启动（SSID: %s）", CONFIG_WIFI_SSID);
    return esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
}

bool wifi_sta_connected(void)
{
    return s_connected;
}
