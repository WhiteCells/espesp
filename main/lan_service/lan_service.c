#include "lan_service/lan_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

typedef struct {
    lan_service_config_t config;
    uint32_t request_count;
} lan_service_context_t;

static const char *TAG = "lan_service";

static esp_err_t set_common_headers(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Cache-Control", "no-store"), TAG, "set Cache-Control");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff"), TAG, "set X-Content-Type-Options");
    return httpd_resp_set_hdr(req, "Connection", "close");
}

static esp_err_t send_plain(httpd_req_t *req, const char *body)
{
    ESP_RETURN_ON_ERROR(set_common_headers(req), TAG, "set common headers");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "text/plain; charset=utf-8"), TAG, "set text/plain");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t send_json(httpd_req_t *req, const char *body)
{
    ESP_RETURN_ON_ERROR(set_common_headers(req), TAG, "set common headers");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "application/json"), TAG, "set application/json");
    return httpd_resp_sendstr(req, body);
}

static bool secure_equals(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }

    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    unsigned char diff = (unsigned char)(left_len ^ right_len);
    size_t max_len = left_len > right_len ? left_len : right_len;

    for (size_t i = 0; i < max_len; i++) {
        unsigned char a = i < left_len ? (unsigned char)left[i] : 0;
        unsigned char b = i < right_len ? (unsigned char)right[i] : 0;
        diff |= (unsigned char)(a ^ b);
    }

    return diff == 0;
}

static bool has_authorization(httpd_req_t *req, const lan_service_context_t *ctx)
{
    if (ctx == NULL || !ctx->config.require_auth) {
        return true;
    }

    if (ctx->config.auth_token == NULL || ctx->config.auth_token[0] == '\0') {
        ESP_LOGE(TAG, "authorization is required but token is empty");
        return false;
    }

    char expected[160];
    int written = snprintf(expected, sizeof(expected), "Bearer %s", ctx->config.auth_token);
    if (written < 0 || written >= (int)sizeof(expected)) {
        ESP_LOGE(TAG, "authorization token is too long");
        return false;
    }

    char actual[160] = { 0 };
    if (httpd_req_get_hdr_value_str(req, "Authorization", actual, sizeof(actual)) != ESP_OK) {
        return false;
    }

    return secure_equals(actual, expected);
}

static esp_err_t require_authorization(httpd_req_t *req, const lan_service_context_t *ctx)
{
    if (has_authorization(req, ctx)) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(set_common_headers(req), TAG, "set common headers");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer"), TAG, "set WWW-Authenticate");
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "unauthorized");
    return ESP_FAIL;
}

static esp_err_t read_request_body(httpd_req_t *req, char *buffer, size_t buffer_len)
{
    if (buffer_len == 0 || buffer_len > CONFIG_ESPESP_LAN_SERVICE_MAX_BODY_LEN ||
        req->content_len >= buffer_len) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "body too large");
        return ESP_FAIL;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buffer + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }

            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to receive body");
            return ESP_FAIL;
        }

        received += ret;
    }

    buffer[received] = '\0';
    return ESP_OK;
}

static uint32_t next_request_count(lan_service_context_t *ctx)
{
    if (ctx == NULL) {
        return 0;
    }

    return ++ctx->request_count;
}

static esp_err_t health_get_handler(httpd_req_t *req)
{
    lan_service_context_t *ctx = (lan_service_context_t *)req->user_ctx;
    uint32_t count = next_request_count(ctx);

    char body[224];
    snprintf(body,
             sizeof(body),
             "{\"status\":\"ok\",\"service\":\"%s\",\"scheme\":\"%s\",\"requests\":%" PRIu32 ",\"uptime_ms\":%" PRId64 ",\"free_heap\":%" PRIu32 "}\n",
             ctx->config.service_name,
             ctx->config.scheme,
             count,
             esp_timer_get_time() / 1000,
             esp_get_free_heap_size());

    return send_json(req, body);
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    lan_service_context_t *ctx = (lan_service_context_t *)req->user_ctx;
    ESP_RETURN_ON_ERROR(require_authorization(req, ctx), TAG, "authorize status");
    uint32_t count = next_request_count(ctx);

    char body[224];
    snprintf(body,
             sizeof(body),
             "{\"service\":\"%s\",\"scheme\":\"%s\",\"requests\":%" PRIu32 ",\"free_heap\":%" PRIu32 "}\n",
             ctx->config.service_name,
             ctx->config.scheme,
             count,
             esp_get_free_heap_size());

    return send_json(req, body);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    lan_service_context_t *ctx = (lan_service_context_t *)req->user_ctx;
    uint32_t count = next_request_count(ctx);

    char body[256];
    snprintf(body,
             sizeof(body),
             "%s\n"
             "scheme=%s\n"
             "requests=%" PRIu32 "\n"
             "routes=/health,/api/v1/status,/api/v1/control\n",
             ctx->config.service_name,
             ctx->config.scheme,
             count);

    return send_plain(req, body);
}

static esp_err_t control_post_handler(httpd_req_t *req)
{
    lan_service_context_t *ctx = (lan_service_context_t *)req->user_ctx;
    ESP_RETURN_ON_ERROR(require_authorization(req, ctx), TAG, "authorize control");
    uint32_t count = next_request_count(ctx);

    char content[CONFIG_ESPESP_LAN_SERVICE_MAX_BODY_LEN] = { 0 };
    esp_err_t ret = read_request_body(req, content, ctx->config.max_body_len);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "%s control request: %s", ctx->config.scheme, content);

    char body[CONFIG_ESPESP_LAN_SERVICE_MAX_BODY_LEN + 128];
    snprintf(body,
             sizeof(body),
             "{\"ok\":true,\"scheme\":\"%s\",\"requests\":%" PRIu32 ",\"accepted\":%u}\n",
             ctx->config.scheme,
             count,
             (unsigned int)strlen(content));

    return send_json(req, body);
}

static esp_err_t not_found_handler(httpd_req_t *req, httpd_err_code_t err)
{
    ESP_LOGW(TAG, "not found: uri=%s, err=%d", req->uri, err);
    ESP_RETURN_ON_ERROR(set_common_headers(req), TAG, "set common headers");
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "route not found");
    return ESP_FAIL;
}

static esp_err_t register_get(httpd_handle_t server,
                              const char *uri,
                              esp_err_t (*handler)(httpd_req_t *),
                              lan_service_context_t *ctx)
{
    const httpd_uri_t route = {
        .uri = uri,
        .method = HTTP_GET,
        .handler = handler,
        .user_ctx = ctx,
    };

    return httpd_register_uri_handler(server, &route);
}

static esp_err_t register_post(httpd_handle_t server,
                               const char *uri,
                               esp_err_t (*handler)(httpd_req_t *),
                               lan_service_context_t *ctx)
{
    const httpd_uri_t route = {
        .uri = uri,
        .method = HTTP_POST,
        .handler = handler,
        .user_ctx = ctx,
    };

    return httpd_register_uri_handler(server, &route);
}

esp_err_t lan_service_register_handlers(httpd_handle_t server, const lan_service_config_t *config)
{
    if (server == NULL || config == NULL || config->service_name == NULL || config->scheme == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lan_service_context_t *ctx = calloc(1, sizeof(lan_service_context_t));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ctx->config = *config;

    esp_err_t ret = register_get(server, "/", root_get_handler, ctx);
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "register / failed");

    ret = register_get(server, "/health*", health_get_handler, ctx);
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "register /health failed");

    ret = register_get(server, "/api/v1/status*", status_get_handler, ctx);
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "register /api/v1/status failed");

    ret = register_post(server, "/api/v1/control*", control_post_handler, ctx);
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "register /api/v1/control failed");

    ret = httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found_handler);
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "register 404 handler failed");

    return ESP_OK;

fail:
    free(ctx);
    return ret;
}
