#include "http_client/http_client.h"

#include <inttypes.h>

#include "esp_check.h"
#include "esp_log.h"
#include "http_client/http_request.h"
#include "http_client/http_response_log.h"
#include "sdkconfig.h"
#include "wifi_station/wifi_station.h"

static const char *TAG = "http_client";

static esp_http_client_config_t http_client_default_client_config(http_response_log_t *response_log)
{
    return (esp_http_client_config_t) {
        .url = CONFIG_ESPESP_HTTP_URL,
        .event_handler = http_response_log_event_handler,
        .user_data = response_log,
        .timeout_ms = CONFIG_ESPESP_HTTP_TIMEOUT_MS,
    };
}

esp_err_t http_client_run(void)
{
    ESP_RETURN_ON_FALSE(CONFIG_ESPESP_HTTP_URL[0] != '\0',
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "HTTP URL is empty. Set it in menuconfig first.");
    ESP_RETURN_ON_ERROR(wifi_station_connect(), TAG, "connect Wi-Fi");

    http_response_log_t response_log = {
        0
    };
    http_response_log_init(&response_log, TAG, CONFIG_ESPESP_HTTP_PRINT_LIMIT);

    esp_http_client_config_t client_config = http_client_default_client_config(&response_log);
    http_request_t request = {
        .operation_name = "GET",
        .log_tag = TAG,
        .client_config = &client_config,
    };
    http_response_meta_t response_meta = { 0 };

    /* HTTP 事件回调边收边打印响应体，避免一次性申请大块缓冲区。 */
    esp_err_t ret = http_request_perform(&request, &response_meta);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "status=%d, content_length=%" PRId64,
             response_meta.status_code,
             response_meta.content_length);
    http_response_log_print_summary(&response_log);
    return ESP_OK;
}
