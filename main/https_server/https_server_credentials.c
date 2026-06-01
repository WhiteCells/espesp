#include "https_server/https_server_credentials.h"

#include <stdlib.h>
#include <string.h>

#include "core/app_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "https_server";

static esp_err_t https_server_read_nvs_string(nvs_handle_t handle, const char *key, char **value)
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

void https_server_credentials_release(https_server_credentials_t *credentials)
{
    if (credentials == NULL) {
        return;
    }

    free(credentials->servercert);
    credentials->servercert = NULL;
    free(credentials->private_key);
    credentials->private_key = NULL;
}

esp_err_t https_server_credentials_load(https_server_credentials_t *credentials)
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

    ret = https_server_read_nvs_string(handle,
                                       CONFIG_ESPESP_HTTPS_CERT_NVS_CERT_KEY,
                                       &credentials->servercert);
    if (ret == ESP_OK) {
        ret = https_server_read_nvs_string(handle,
                                           CONFIG_ESPESP_HTTPS_CERT_NVS_PRIVATE_KEY,
                                           &credentials->private_key);
    }

    nvs_close(handle);

    if (ret != ESP_OK) {
        https_server_credentials_release(credentials);
        ESP_LOGE(TAG, "failed to load HTTPS certificate or key from NVS: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t https_server_credentials_validate(const https_server_credentials_t *credentials)
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
