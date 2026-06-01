#include "http_server/http_server.h"

#include "http_server/http_server_runtime.h"

#include "wifi_station/wifi_station.h"

esp_err_t http_server_run(void)
{
    http_server_runtime_t runtime = { 0 };

    esp_err_t ret = http_server_runtime_prepare(&runtime);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = wifi_station_connect();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = http_server_runtime_start(&runtime);
    if (ret != ESP_OK) {
        return ret;
    }

    http_server_runtime_log_startup(&runtime);
    http_server_runtime_wait_forever();
    return ESP_OK;
}
