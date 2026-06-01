#include "websocket_server/websocket_server.h"

#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "websocket_server/websocket_server_messages.h"
#include "websocket_server/websocket_server_runtime.h"
#include "wifi_station/wifi_station.h"

const char *WEBSOCKET_SERVER_TAG = "websocket_server";

esp_err_t websocket_server_run(void)
{
#if !CONFIG_HTTPD_WS_SUPPORT
    ESP_LOGE(WEBSOCKET_SERVER_TAG,
             "ESP-IDF HTTPD WebSocket support is disabled. Enable CONFIG_HTTPD_WS_SUPPORT.");
    return ESP_ERR_NOT_SUPPORTED;
#else
    websocket_server_runtime_t runtime = { 0 };

    esp_err_t ret = websocket_server_runtime_prepare(&runtime);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = wifi_station_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(WEBSOCKET_SERVER_TAG,
                 "failed to connect Wi-Fi before starting websocket server: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = websocket_server_runtime_start(&runtime);
    if (ret != ESP_OK) {
        return ret;
    }

    websocket_server_runtime_log_startup(&runtime);

    uint32_t sequence = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(runtime.server_config.publish_period_ms));

        if (runtime.context == NULL || runtime.context->server == NULL) {
            continue;
        }

        ret = websocket_server_broadcast_status(runtime.context, sequence++);
        if (ret != ESP_OK) {
            ESP_LOGW(WEBSOCKET_SERVER_TAG, "status broadcast failed: %s", esp_err_to_name(ret));
        }
    }

    return ESP_OK;
#endif
}
