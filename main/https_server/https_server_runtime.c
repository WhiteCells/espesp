#include "https_server/https_server_runtime.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "https_server/https_server_credentials.h"

static const char *TAG = "https_server";

static esp_err_t https_server_validate_auth_config(void)
{
    if (ESPESP_LAN_SERVICE_REQUIRE_AUTH && CONFIG_ESPESP_LAN_SERVICE_AUTH_TOKEN[0] == '\0') {
        ESP_LOGE(TAG, "LAN service auth token is empty. Set it in menuconfig before exposing the server.");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static httpd_ssl_config_t https_server_default_config(void)
{
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.port_secure = CONFIG_ESPESP_HTTPS_SERVER_PORT;
    config.httpd.ctrl_port = CONFIG_ESPESP_HTTPS_SERVER_CTRL_PORT;
    config.httpd.stack_size = CONFIG_ESPESP_HTTPS_SERVER_STACK_SIZE;
    config.httpd.max_open_sockets = CONFIG_ESPESP_HTTPS_SERVER_MAX_OPEN_SOCKETS;
    config.httpd.recv_wait_timeout = CONFIG_ESPESP_LAN_SERVICE_RECV_TIMEOUT_SEC;
    config.httpd.send_wait_timeout = CONFIG_ESPESP_LAN_SERVICE_SEND_TIMEOUT_SEC;
    config.httpd.uri_match_fn = httpd_uri_match_wildcard;
    config.tls_handshake_timeout_ms = CONFIG_ESPESP_HTTPS_HANDSHAKE_TIMEOUT_MS;
    return config;
}

static lan_service_config_t https_server_default_service_config(void)
{
    return (lan_service_config_t) {
        .service_name = "espesp-lan-https",
        .scheme = "https",
        .auth_token = CONFIG_ESPESP_LAN_SERVICE_AUTH_TOKEN,
        .max_body_len = CONFIG_ESPESP_LAN_SERVICE_MAX_BODY_LEN,
        .require_auth = ESPESP_LAN_SERVICE_REQUIRE_AUTH,
    };
}

static void https_server_runtime_stop(https_server_runtime_t *runtime)
{
    if (runtime == NULL || runtime->handle == NULL) {
        return;
    }

    httpd_ssl_stop(runtime->handle);
    runtime->handle = NULL;
}

static void https_server_runtime_attach_credentials(httpd_ssl_config_t *config,
                                                    const https_server_credentials_t *credentials)
{
    config->servercert = (const uint8_t *)credentials->servercert;
    config->servercert_len = strlen(credentials->servercert) + 1;
    config->prvtkey_pem = (const uint8_t *)credentials->private_key;
    config->prvtkey_len = strlen(credentials->private_key) + 1;
}

static void https_server_runtime_clear_credentials(httpd_ssl_config_t *config)
{
    config->servercert = NULL;
    config->servercert_len = 0;
    config->prvtkey_pem = NULL;
    config->prvtkey_len = 0;
}

esp_err_t https_server_runtime_prepare(https_server_runtime_t *runtime)
{
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = https_server_validate_auth_config();
    if (ret != ESP_OK) {
        return ret;
    }

    runtime->handle = NULL;
    runtime->httpd_config = https_server_default_config();
    runtime->service_config = https_server_default_service_config();
    return ESP_OK;
}

esp_err_t https_server_runtime_start(https_server_runtime_t *runtime)
{
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    https_server_credentials_t credentials = { 0 };
    esp_err_t ret = https_server_credentials_load(&credentials);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = https_server_credentials_validate(&credentials);
    if (ret != ESP_OK) {
        https_server_credentials_release(&credentials);
        return ret;
    }

    https_server_runtime_attach_credentials(&runtime->httpd_config, &credentials);
    ret = httpd_ssl_start(&runtime->handle, &runtime->httpd_config);
    https_server_credentials_release(&credentials);
    https_server_runtime_clear_credentials(&runtime->httpd_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTPS server: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = lan_service_register_handlers(runtime->handle, &runtime->service_config);
    if (ret != ESP_OK) {
        https_server_runtime_stop(runtime);
    }

    return ret;
}

void https_server_runtime_log_startup(const https_server_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    ESP_LOGI(TAG, "HTTPS server started on port %d", runtime->httpd_config.port_secure);
    if (runtime->service_config.require_auth) {
        ESP_LOGI(TAG, "LAN control routes require Authorization: Bearer <token>");
    } else {
        ESP_LOGI(TAG, "LAN control routes are running without bearer token auth");
    }
}

void https_server_runtime_wait_forever(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
