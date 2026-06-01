#include "http_server/http_server_runtime.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "http_server";

static esp_err_t http_server_validate_auth_config(void)
{
    if (ESPESP_LAN_SERVICE_REQUIRE_AUTH && CONFIG_ESPESP_LAN_SERVICE_AUTH_TOKEN[0] == '\0') {
        ESP_LOGE(TAG, "LAN service auth token is empty. Set it in menuconfig before exposing the server.");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static httpd_config_t http_server_default_config(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_ESPESP_HTTP_SERVER_PORT;
    config.ctrl_port = CONFIG_ESPESP_HTTP_SERVER_CTRL_PORT;
    config.stack_size = CONFIG_ESPESP_HTTP_SERVER_STACK_SIZE;
    config.max_open_sockets = CONFIG_ESPESP_HTTP_SERVER_MAX_OPEN_SOCKETS;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = CONFIG_ESPESP_LAN_SERVICE_RECV_TIMEOUT_SEC;
    config.send_wait_timeout = CONFIG_ESPESP_LAN_SERVICE_SEND_TIMEOUT_SEC;
    config.uri_match_fn = httpd_uri_match_wildcard;

    return config;
}

static lan_service_config_t http_server_default_service_config(void)
{
    return (lan_service_config_t) {
        .service_name = "espesp-lan-http",
        .scheme = "http",
        .auth_token = CONFIG_ESPESP_LAN_SERVICE_AUTH_TOKEN,
        .max_body_len = CONFIG_ESPESP_LAN_SERVICE_MAX_BODY_LEN,
        .require_auth = ESPESP_LAN_SERVICE_REQUIRE_AUTH,
    };
}

static void http_server_runtime_stop(http_server_runtime_t *runtime)
{
    if (runtime == NULL || runtime->handle == NULL) {
        return;
    }

    httpd_stop(runtime->handle);
    runtime->handle = NULL;
}

esp_err_t http_server_runtime_prepare(http_server_runtime_t *runtime)
{
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = http_server_validate_auth_config();
    if (ret != ESP_OK) {
        return ret;
    }

    runtime->handle = NULL;
    runtime->httpd_config = http_server_default_config();
    runtime->service_config = http_server_default_service_config();
    return ESP_OK;
}

esp_err_t http_server_runtime_start(http_server_runtime_t *runtime)
{
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = httpd_start(&runtime->handle, &runtime->httpd_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = lan_service_register_handlers(runtime->handle, &runtime->service_config);
    if (ret != ESP_OK) {
        http_server_runtime_stop(runtime);
    }

    return ret;
}

void http_server_runtime_log_startup(const http_server_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", runtime->httpd_config.server_port);
    if (runtime->service_config.require_auth) {
        ESP_LOGI(TAG, "LAN control routes require Authorization: Bearer <token>");
    } else {
        ESP_LOGI(TAG, "LAN control routes are running without bearer token auth");
    }
}

void http_server_runtime_wait_forever(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
