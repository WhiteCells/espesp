#include "https_client/https_client.h"

#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "http_client/http_request.h"
#include "http_client/http_response_log.h"
#include "https_client/https_client_security.h"
#include "sdkconfig.h"
#include "wifi_station/wifi_station.h"

static const char *TAG = "https_client";

static bool https_client_url_is_valid(const char *url)
{
    return url != NULL && strncmp(url, "https://", strlen("https://")) == 0;
}

static esp_http_client_config_t https_client_default_client_config(http_response_log_t *response_log)
{
    return (esp_http_client_config_t) {
        .url = CONFIG_ESPESP_HTTPS_CLIENT_URL,
        .event_handler = http_response_log_event_handler,
        .user_data = response_log,
        .timeout_ms = CONFIG_ESPESP_HTTPS_CLIENT_TIMEOUT_MS,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
    };
}

esp_err_t https_client_run(void)
{
    ESP_RETURN_ON_FALSE(https_client_url_is_valid(CONFIG_ESPESP_HTTPS_CLIENT_URL),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "HTTPS client URL must start with https://");
    ESP_RETURN_ON_ERROR(wifi_station_connect(), TAG, "connect Wi-Fi");

    http_response_log_t response_log = { 0 };
    http_response_log_init(&response_log, TAG, CONFIG_ESPESP_HTTPS_CLIENT_PRINT_LIMIT);

    https_client_security_t security = { 0 };
    ESP_RETURN_ON_ERROR(https_client_security_prepare(&security), TAG, "prepare TLS security");

    esp_http_client_config_t client_config = https_client_default_client_config(&response_log);
    https_client_security_apply(&client_config, &security);

    http_request_t request = {
        .operation_name = "GET",
        .log_tag = TAG,
        .client_config = &client_config,
    };
    http_response_meta_t response_meta = { 0 };

    /* HTTPS 传输层仍复用 HTTP client 事件回调，差异只在 TLS 校验与证书来源。 */
    esp_err_t ret = http_request_perform(&request, &response_meta);
    https_client_security_release(&security);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "status=%d, content_length=%" PRId64,
             response_meta.status_code,
             response_meta.content_length);
    http_response_log_print_summary(&response_log);
    return ESP_OK;
}
