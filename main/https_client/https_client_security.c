#include "https_client/https_client_security.h"

#include <stdlib.h>
#include <string.h>

#include "core/app_common.h"
#include "esp_check.h"
#include "esp_log.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "https_client";

#if CONFIG_ESPESP_HTTPS_CLIENT_VERIFY_NVS_CA
static esp_err_t https_client_read_nvs_string(nvs_handle_t handle, const char *key, char **value)
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

static esp_err_t https_client_load_trusted_ca_from_nvs(https_client_security_t *security)
{
    ESP_RETURN_ON_ERROR(app_common_init_nvs(), TAG, "init NVS");

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "HTTPS client CA cert not found. Store a PEM certificate in NVS namespace '%s' key '%s'.",
                 CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_NAMESPACE,
                 CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_CERT_KEY);
        return ret;
    }

    ret = https_client_read_nvs_string(handle,
                                       CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_CERT_KEY,
                                       &security->trusted_cert_pem);
    nvs_close(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to load HTTPS client CA cert from NVS: %s", esp_err_to_name(ret));
    }

    return ret;
}

static esp_err_t https_client_validate_trusted_ca(const https_client_security_t *security)
{
    if (security == NULL || security->trusted_cert_pem == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strstr(security->trusted_cert_pem, "-----BEGIN CERTIFICATE-----") == NULL) {
        ESP_LOGE(TAG, "HTTPS client CA cert in NVS is not a PEM certificate string");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}
#endif

esp_err_t https_client_security_prepare(https_client_security_t *security)
{
    if (security == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *security = (https_client_security_t) {
#if CONFIG_ESPESP_HTTPS_CLIENT_SKIP_COMMON_NAME_CHECK
        .skip_common_name_check = true,
#else
        .skip_common_name_check = false,
#endif
    };

#if CONFIG_ESPESP_HTTPS_CLIENT_VERIFY_BUNDLE
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    security->crt_bundle_attach = esp_crt_bundle_attach;
    ESP_LOGI(TAG, "TLS verify mode: ESP x509 certificate bundle");
#else
    ESP_LOGE(TAG, "certificate bundle verify mode requires CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y");
    return ESP_ERR_NOT_SUPPORTED;
#endif
#elif CONFIG_ESPESP_HTTPS_CLIENT_VERIFY_NVS_CA
    esp_err_t ret = https_client_load_trusted_ca_from_nvs(security);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = https_client_validate_trusted_ca(security);
    if (ret != ESP_OK) {
        https_client_security_release(security);
        return ret;
    }

    ESP_LOGI(TAG,
             "TLS verify mode: NVS CA cert namespace='%s' key='%s'",
             CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_NAMESPACE,
             CONFIG_ESPESP_HTTPS_CLIENT_CERT_NVS_CERT_KEY);
#else
#error "HTTPS client verify mode is not configured"
#endif

    if (security->skip_common_name_check) {
        ESP_LOGW(TAG,
                 "TLS common name check is disabled. Use only for controlled self-signed tests.");
    }

    return ESP_OK;
}

void https_client_security_apply(esp_http_client_config_t *client_config,
                                 const https_client_security_t *security)
{
    if (client_config == NULL || security == NULL) {
        return;
    }

    client_config->skip_cert_common_name_check = security->skip_common_name_check;

    if (security->crt_bundle_attach != NULL) {
        client_config->crt_bundle_attach = security->crt_bundle_attach;
    }

    if (security->trusted_cert_pem != NULL) {
        client_config->cert_pem = security->trusted_cert_pem;
        client_config->cert_len = strlen(security->trusted_cert_pem) + 1;
    }
}

void https_client_security_release(https_client_security_t *security)
{
    if (security == NULL) {
        return;
    }

    free(security->trusted_cert_pem);
    security->trusted_cert_pem = NULL;
    security->crt_bundle_attach = NULL;
}
