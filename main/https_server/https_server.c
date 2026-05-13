#include "https_server/https_server.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "core/app_common.h"
#include "lan_service/lan_service.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "wifi_station/wifi_station.h"

typedef struct {
    char *servercert;
    char *private_key;
} https_credentials_t;

static const char *TAG = "https_server";

static void https_credentials_free(https_credentials_t *credentials)
{
    if (credentials == NULL) {
        return;
    }

    free(credentials->servercert);
    credentials->servercert = NULL;
    free(credentials->private_key);
    credentials->private_key = NULL;
}

static esp_err_t read_nvs_string(nvs_handle_t handle, const char *key, char **value)
{
    size_t required_size = 0;
    esp_err_t ret = nvs_get_str(handle, key, NULL, &required_size);
    if (ret != ESP_OK) {
        return ret;
    }

    char *buffer = calloc(1, required_size);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ret = nvs_get_str(handle, key, buffer, &required_size);
    if (ret != ESP_OK) {
        free(buffer);
        return ret;
    }

    *value = buffer;
    return ESP_OK;
}

static esp_err_t load_https_credentials(https_credentials_t *credentials)
{
    if (credentials == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(app_common_init_nvs(), TAG, "init NVS");

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(CONFIG_ESPESP_HTTPS_CERT_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "HTTPS credentials not found. Store PEM strings in NVS namespace '%s' keys '%s' and '%s'.",
                 CONFIG_ESPESP_HTTPS_CERT_NVS_NAMESPACE,
                 CONFIG_ESPESP_HTTPS_CERT_NVS_CERT_KEY,
                 CONFIG_ESPESP_HTTPS_CERT_NVS_PRIVATE_KEY);
        return ret;
    }

    ret = read_nvs_string(handle, CONFIG_ESPESP_HTTPS_CERT_NVS_CERT_KEY, &credentials->servercert);
    if (ret == ESP_OK) {
        ret = read_nvs_string(handle, CONFIG_ESPESP_HTTPS_CERT_NVS_PRIVATE_KEY, &credentials->private_key);
    }

    nvs_close(handle);

    if (ret != ESP_OK) {
        https_credentials_free(credentials);
        ESP_LOGE(TAG, "failed to load HTTPS certificate or key from NVS: %s", esp_err_to_name(ret));
    }

    return ret;
}

static esp_err_t validate_https_credentials(const https_credentials_t *credentials)
{
    if (credentials == NULL || credentials->servercert == NULL || credentials->private_key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strstr(credentials->servercert, "-----BEGIN CERTIFICATE-----") == NULL ||
        strstr(credentials->private_key, "-----BEGIN") == NULL ||
        strstr(credentials->private_key, "PRIVATE KEY-----") == NULL) {
        ESP_LOGE(TAG, "HTTPS credentials in NVS are not PEM certificate/private key strings");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t https_server_run(void)
{
    if (ESPESP_LAN_SERVICE_REQUIRE_AUTH && CONFIG_ESPESP_LAN_SERVICE_AUTH_TOKEN[0] == '\0') {
        ESP_LOGE(TAG, "LAN service auth token is empty. Set it in menuconfig before exposing the server.");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(wifi_station_connect());

    https_credentials_t credentials = { 0 };
    esp_err_t ret = load_https_credentials(&credentials);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = validate_https_credentials(&credentials);
    if (ret != ESP_OK) {
        https_credentials_free(&credentials);
        return ret;
    }

    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.port_secure = CONFIG_ESPESP_HTTPS_SERVER_PORT;
    config.httpd.ctrl_port = CONFIG_ESPESP_HTTPS_SERVER_CTRL_PORT;
    config.httpd.stack_size = CONFIG_ESPESP_HTTPS_SERVER_STACK_SIZE;
    config.httpd.max_open_sockets = CONFIG_ESPESP_HTTPS_SERVER_MAX_OPEN_SOCKETS;
    config.httpd.recv_wait_timeout = CONFIG_ESPESP_LAN_SERVICE_RECV_TIMEOUT_SEC;
    config.httpd.send_wait_timeout = CONFIG_ESPESP_LAN_SERVICE_SEND_TIMEOUT_SEC;
    config.httpd.uri_match_fn = httpd_uri_match_wildcard;
    config.tls_handshake_timeout_ms = CONFIG_ESPESP_HTTPS_HANDSHAKE_TIMEOUT_MS;
    config.servercert = (const uint8_t *)credentials.servercert;
    config.servercert_len = strlen(credentials.servercert) + 1;
    config.prvtkey_pem = (const uint8_t *)credentials.private_key;
    config.prvtkey_len = strlen(credentials.private_key) + 1;

    httpd_handle_t server = NULL;
    ret = httpd_ssl_start(&server, &config);
    https_credentials_free(&credentials);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTPS server: %s", esp_err_to_name(ret));
        return ret;
    }

    const lan_service_config_t service_config = {
        .service_name = "espesp-lan-https",
        .scheme = "https",
        .auth_token = CONFIG_ESPESP_LAN_SERVICE_AUTH_TOKEN,
        .max_body_len = CONFIG_ESPESP_LAN_SERVICE_MAX_BODY_LEN,
        .require_auth = ESPESP_LAN_SERVICE_REQUIRE_AUTH,
    };

    ret = lan_service_register_handlers(server, &service_config);
    if (ret != ESP_OK) {
        httpd_ssl_stop(server);
        return ret;
    }

    ESP_LOGI(TAG, "HTTPS server started on port %d", CONFIG_ESPESP_HTTPS_SERVER_PORT);
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
