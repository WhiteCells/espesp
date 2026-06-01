#include "http_client/http_request.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "http_request";

static void http_response_meta_reset(http_response_meta_t *response_meta)
{
    if (response_meta == NULL) {
        return;
    }

    response_meta->status_code = -1;
    response_meta->content_length = -1;
}

esp_err_t http_request_perform(const http_request_t *request,
                               http_response_meta_t *response_meta)
{
    http_response_meta_reset(response_meta);

    ESP_RETURN_ON_FALSE(request != NULL, ESP_ERR_INVALID_ARG, TAG, "request is required");
    ESP_RETURN_ON_FALSE(request->client_config != NULL,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "client_config is required");
    ESP_RETURN_ON_FALSE(request->client_config->url != NULL &&
                            request->client_config->url[0] != '\0',
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "HTTP URL is empty");

    const char *log_tag = request->log_tag != NULL ? request->log_tag : TAG;
    const char *operation_name =
        request->operation_name != NULL ? request->operation_name : "HTTP request";

    esp_http_client_handle_t client = esp_http_client_init(request->client_config);
    if (client == NULL) {
        ESP_LOGE(log_tag, "failed to allocate HTTP client");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(log_tag, "%s %s", operation_name, request->client_config->url);
    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK && response_meta != NULL) {
        response_meta->status_code = esp_http_client_get_status_code(client);
        response_meta->content_length = esp_http_client_get_content_length(client);
    } else if (ret != ESP_OK) {
        ESP_LOGE(log_tag, "request failed: %s", esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    return ret;
}
