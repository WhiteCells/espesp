#include "http_client/http_response_log.h"

#include "esp_log.h"

static const char *TAG = "http_resp_log";

static const char *http_response_log_tag(const http_response_log_t *response_log)
{
    if (response_log != NULL && response_log->tag != NULL && response_log->tag[0] != '\0') {
        return response_log->tag;
    }

    return TAG;
}

static void http_response_log_limit_reached(http_response_log_t *response_log)
{
    if (response_log == NULL || response_log->limit_reached) {
        return;
    }

    ESP_LOGW(http_response_log_tag(response_log),
             "body output reached limit=%d bytes, remaining data is skipped",
             response_log->print_limit);
    response_log->limit_reached = true;
}

static void http_response_log_body_chunk(http_response_log_t *response_log,
                                         const esp_http_client_event_t *evt)
{
    if (response_log == NULL || evt == NULL || evt->data == NULL || evt->data_len <= 0) {
        return;
    }

    if (response_log->print_limit <= 0) {
        return;
    }

    if (response_log->printed_bytes >= response_log->print_limit) {
        http_response_log_limit_reached(response_log);
        return;
    }

    int remaining = response_log->print_limit - response_log->printed_bytes;
    int to_print = evt->data_len < remaining ? evt->data_len : remaining;

    ESP_LOGI(http_response_log_tag(response_log),
             "body chunk (%d bytes): %.*s",
             to_print,
             to_print,
             (const char *)evt->data);
    response_log->printed_bytes += to_print;

    if (evt->data_len > to_print) {
        http_response_log_limit_reached(response_log);
    }
}

void http_response_log_init(http_response_log_t *response_log,
                            const char *tag,
                            int print_limit)
{
    if (response_log == NULL) {
        return;
    }

    *response_log = (http_response_log_t) {
        .tag = tag,
        .printed_bytes = 0,
        .print_limit = print_limit,
        .limit_reached = false,
    };
}

esp_err_t http_response_log_event_handler(esp_http_client_event_t *evt)
{
    if (evt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    http_response_log_t *response_log = (http_response_log_t *)evt->user_data;
    const char *tag = http_response_log_tag(response_log);

    switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(tag, "HTTP connected");
        break;
    case HTTP_EVENT_ON_HEADER:
        if (evt->header_key != NULL && evt->header_value != NULL) {
            ESP_LOGI(tag, "header: %s: %s", evt->header_key, evt->header_value);
        }
        break;
    case HTTP_EVENT_ON_DATA:
        http_response_log_body_chunk(response_log, evt);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(tag,
                 "HTTP transfer finished, printed_body_bytes=%d",
                 response_log != NULL ? response_log->printed_bytes : 0);
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(tag, "HTTP disconnected");
        break;
    default:
        break;
    }

    return ESP_OK;
}

void http_response_log_print_summary(const http_response_log_t *response_log)
{
    if (response_log == NULL) {
        return;
    }

    if (response_log->print_limit <= 0) {
        ESP_LOGI(http_response_log_tag(response_log), "response body logging disabled");
        return;
    }

    ESP_LOGI(http_response_log_tag(response_log),
             "response body was printed in HTTP_EVENT_ON_DATA, limit=%d bytes",
             response_log->print_limit);
}
