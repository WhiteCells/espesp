#include "http_server/http_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "lan_service/lan_service.h"
#include "sdkconfig.h"
#include "wifi_station/wifi_station.h"

static const char *TAG = "http_server";

esp_err_t http_server_run(void)
{
    if (ESPESP_LAN_SERVICE_REQUIRE_AUTH && CONFIG_ESPESP_LAN_SERVICE_AUTH_TOKEN[0] == '\0') {
        ESP_LOGE(TAG, "LAN service auth token is empty. Set it in menuconfig before exposing the server.");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(wifi_station_connect());

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_ESPESP_HTTP_SERVER_PORT;
    config.ctrl_port = CONFIG_ESPESP_HTTP_SERVER_CTRL_PORT;
    config.stack_size = CONFIG_ESPESP_HTTP_SERVER_STACK_SIZE;
    config.max_open_sockets = CONFIG_ESPESP_HTTP_SERVER_MAX_OPEN_SOCKETS;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = CONFIG_ESPESP_LAN_SERVICE_RECV_TIMEOUT_SEC;
    config.send_wait_timeout = CONFIG_ESPESP_LAN_SERVICE_SEND_TIMEOUT_SEC;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    const lan_service_config_t service_config = {
        .service_name = "espesp-lan-http",
        .scheme = "http",
        .auth_token = CONFIG_ESPESP_LAN_SERVICE_AUTH_TOKEN,
        .max_body_len = CONFIG_ESPESP_LAN_SERVICE_MAX_BODY_LEN,
        .require_auth = ESPESP_LAN_SERVICE_REQUIRE_AUTH,
    };

    ret = lan_service_register_handlers(server, &service_config);
    if (ret != ESP_OK) {
        httpd_stop(server);
        return ret;
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", CONFIG_ESPESP_HTTP_SERVER_PORT);
    if (ESPESP_LAN_SERVICE_REQUIRE_AUTH) {
        ESP_LOGI(TAG, "LAN control routes require Authorization: Bearer <token>");
    } else {
        ESP_LOGI(TAG, "LAN control routes are running without bearer token auth");
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    return ESP_OK;
}
