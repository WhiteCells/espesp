#include "http_client/http_get.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "wifi_station/wifi_station.h"

typedef struct {
    int printed_bytes;
    int print_limit;
} http_get_context_t;

static const char *TAG = "http_get";

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_get_context_t *ctx = (http_get_context_t *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP connected");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI(TAG, "header: %s: %s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        if (ctx != NULL && ctx->print_limit > 0 && ctx->printed_bytes < ctx->print_limit) {
            int remaining = ctx->print_limit - ctx->printed_bytes;
            int to_print = evt->data_len < remaining ? evt->data_len : remaining;

            ESP_LOGI(TAG, "body chunk (%d bytes): %.*s", to_print, to_print, (const char *)evt->data);
            ctx->printed_bytes += to_print;

            if (ctx->printed_bytes >= ctx->print_limit && evt->data_len > to_print) {
                ESP_LOGW(TAG, "body output reached limit=%d bytes, remaining data is skipped",
                         ctx->print_limit);
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP transfer finished, printed_body_bytes=%d",
                 ctx != NULL ? ctx->printed_bytes : 0);
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP disconnected");
        break;
    default:
        break;
    }

    return ESP_OK;
}

esp_err_t http_get_run(void)
{
    ESP_ERROR_CHECK(wifi_station_connect());

    http_get_context_t ctx = {
        .printed_bytes = 0,
        .print_limit = CONFIG_ESPESP_HTTP_PRINT_LIMIT,
    };
    esp_http_client_config_t config = {
        .url = CONFIG_ESPESP_HTTP_URL,
        .event_handler = http_event_handler,
        .user_data = &ctx,
        .timeout_ms = CONFIG_ESPESP_HTTP_TIMEOUT_MS,
    };

    /* esp_http_client 事件回调适合边收边处理，避免一次性占用大块内存。 */
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "GET %s", CONFIG_ESPESP_HTTP_URL);
    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "status=%d, content_length=%" PRId64,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
        ESP_LOGI(TAG, "response body was printed in HTTP_EVENT_ON_DATA, limit=%d bytes",
                 ctx.print_limit);
    } else {
        ESP_LOGE(TAG, "request failed: %s", esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    return ret;
}
