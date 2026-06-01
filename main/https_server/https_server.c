#include "https_server/https_server.h"

#include "https_server/https_server_runtime.h"

#include "wifi_station/wifi_station.h"

esp_err_t https_server_run(void)
{
    https_server_runtime_t runtime = { 0 };

    esp_err_t ret = https_server_runtime_prepare(&runtime);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = wifi_station_connect();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = https_server_runtime_start(&runtime);
    if (ret != ESP_OK) {
        return ret;
    }

    https_server_runtime_log_startup(&runtime);
    https_server_runtime_wait_forever();
    return ESP_OK;
}
