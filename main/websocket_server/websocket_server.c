#include "websocket_server/websocket_server.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "wifi_station/wifi_station.h"

#define WS_SERVER_HTTPD_SOCKET_RESERVE 3
#define WS_SERVER_AUTH_HEADER_MAX 256
#define WS_SERVER_BINARY_PREVIEW_BYTES 16

typedef struct {
    httpd_handle_t server;
    const char *path;
    const char *auth_token;
    uint32_t publish_period_ms;
    size_t max_clients;
} websocket_server_context_t;

static const char *TAG = "websocket_server";

static websocket_server_context_t *websocket_server_context(httpd_handle_t handle)
{
    return (websocket_server_context_t *)httpd_get_global_user_ctx(handle);
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

static size_t websocket_server_active_client_count(httpd_handle_t handle)
{
    int client_fds[CONFIG_ESPESP_WS_SERVER_MAX_CLIENTS + WS_SERVER_HTTPD_SOCKET_RESERVE];
    size_t fd_count = sizeof(client_fds) / sizeof(client_fds[0]);
    esp_err_t ret = httpd_get_client_list(handle, &fd_count, client_fds);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "failed to list websocket clients: %s", esp_err_to_name(ret));
        return 0;
    }

    size_t websocket_count = 0;
    for (size_t i = 0; i < fd_count; i++) {
        if (httpd_ws_get_fd_info(handle, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            websocket_count++;
        }
    }

    return websocket_count;
}

static void websocket_server_binary_preview(const uint8_t *data, size_t len, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    if (data == NULL || len == 0) {
        snprintf(out, out_len, "(empty)");
        return;
    }

    size_t preview_len = len < WS_SERVER_BINARY_PREVIEW_BYTES ? len : WS_SERVER_BINARY_PREVIEW_BYTES;
    size_t pos = 0;

    for (size_t i = 0; i < preview_len && pos + 4 < out_len; i++) {
        int written = snprintf(out + pos, out_len - pos, "%02X%s", data[i], i + 1 < preview_len ? " " : "");
        if (written < 0) {
            break;
        }

        if ((size_t)written >= out_len - pos) {
            pos = out_len - 1;
            break;
        }

        pos += (size_t)written;
    }

    if (len > preview_len && pos + 5 < out_len) {
        snprintf(out + pos, out_len - pos, " ...");
    } else {
        out[pos] = '\0';
    }
}

static esp_err_t websocket_server_broadcast_status(const websocket_server_context_t *ctx, uint32_t sequence)
{
    if (ctx == NULL || ctx->server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int client_fds[CONFIG_ESPESP_WS_SERVER_MAX_CLIENTS + WS_SERVER_HTTPD_SOCKET_RESERVE];
    size_t fd_count = sizeof(client_fds) / sizeof(client_fds[0]);
    esp_err_t ret = httpd_get_client_list(ctx->server, &fd_count, client_fds);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t websocket_count = 0;
    for (size_t i = 0; i < fd_count; i++) {
        if (httpd_ws_get_fd_info(ctx->server, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            websocket_count++;
        }
    }

    if (websocket_count == 0) {
        ESP_LOGD(TAG, "status tick seq=%" PRIu32 " skipped: no websocket clients", sequence);
        return ESP_OK;
    }

    char payload[256];
    int written = snprintf(payload,
                           sizeof(payload),
                           "{\"type\":\"status\",\"service\":\"websocket_server\",\"path\":\"%s\",\"seq\":%" PRIu32 ",\"clients\":%zu,\"free_heap\":%" PRIu32 ",\"uptime_ms\":%" PRId64 "}",
                           ctx->path,
                           sequence,
                           websocket_count,
                           esp_get_free_heap_size(),
                           esp_timer_get_time() / 1000);
    if (written < 0 || written >= (int)sizeof(payload)) {
        ESP_LOGE(TAG, "status payload too large");
        return ESP_FAIL;
    }

    esp_err_t last_error = ESP_OK;
    for (size_t i = 0; i < fd_count; i++) {
        int sockfd = client_fds[i];
        if (httpd_ws_get_fd_info(ctx->server, sockfd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            continue;
        }

        httpd_ws_frame_t frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)payload,
            .len = (size_t)written,
        };

        ret = httpd_ws_send_data(ctx->server, sockfd, &frame);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "broadcast seq=%" PRIu32 " to fd=%d failed: %s", sequence, sockfd, esp_err_to_name(ret));
            last_error = ret;
        }
    }

    return last_error;
}

static esp_err_t websocket_server_pre_handshake_cb(httpd_req_t *req)
{
    websocket_server_context_t *ctx = websocket_server_context(req->handle);
    if (ctx == NULL) {
        ESP_LOGE(TAG, "missing websocket context");
        return ESP_FAIL;
    }

    if (ctx->auth_token != NULL && ctx->auth_token[0] != '\0') {
        char expected[WS_SERVER_AUTH_HEADER_MAX];
        int expected_len = snprintf(expected, sizeof(expected), "Bearer %s", ctx->auth_token);
        if (expected_len < 0 || expected_len >= (int)sizeof(expected)) {
            ESP_LOGE(TAG, "websocket auth token is too long");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "auth token too long");
            return ESP_FAIL;
        }

        char actual[WS_SERVER_AUTH_HEADER_MAX] = { 0 };
        if (httpd_req_get_hdr_value_str(req, "Authorization", actual, sizeof(actual)) != ESP_OK ||
            !secure_equals(actual, expected)) {
            ESP_LOGW(TAG, "reject websocket handshake from uri=%s due to auth", req->uri);
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
            httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "unauthorized");
            return ESP_FAIL;
        }
    }

    size_t websocket_count = websocket_server_active_client_count(req->handle);
    if (websocket_count >= ctx->max_clients) {
        ESP_LOGW(TAG,
                 "reject websocket handshake from uri=%s due to max clients: %zu >= %zu",
                 req->uri,
                 websocket_count,
                 ctx->max_clients);
        httpd_resp_send_custom_err(req, "503 Service Unavailable", "too many websocket clients");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t websocket_server_post_handshake_cb(httpd_req_t *req)
{
    websocket_server_context_t *ctx = websocket_server_context(req->handle);
    if (ctx == NULL) {
        return ESP_FAIL;
    }

    int sockfd = httpd_req_to_sockfd(req);
    size_t websocket_count = websocket_server_active_client_count(req->handle);

    char payload[256];
    int written = snprintf(payload,
                           sizeof(payload),
                           "{\"type\":\"hello\",\"service\":\"websocket_server\",\"path\":\"%s\",\"clients\":%zu,\"free_heap\":%" PRIu32 ",\"uptime_ms\":%" PRId64 "}",
                           ctx->path,
                           websocket_count,
                           esp_get_free_heap_size(),
                           esp_timer_get_time() / 1000);
    if (written < 0 || written >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "welcome payload too large");
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)payload,
        .len = (size_t)written,
    };

    esp_err_t ret = httpd_ws_send_frame(req, &frame);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "failed to send welcome frame to fd=%d: %s", sockfd, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "websocket connected fd=%d path=%s", sockfd, ctx->path);
    }

    return ESP_OK;
}

static void websocket_server_close_fn(httpd_handle_t hd, int sockfd)
{
    httpd_ws_client_info_t info = httpd_ws_get_fd_info(hd, sockfd);

    switch (info) {
    case HTTPD_WS_CLIENT_WEBSOCKET:
        ESP_LOGI(TAG, "websocket disconnected fd=%d", sockfd);
        break;
    case HTTPD_WS_CLIENT_HTTP:
        ESP_LOGD(TAG, "http session closed fd=%d", sockfd);
        break;
    default:
        ESP_LOGD(TAG, "session closed fd=%d", sockfd);
        break;
    }
}

static esp_err_t websocket_server_frame_handler(httpd_req_t *req)
{
    int sockfd = httpd_req_to_sockfd(req);
    httpd_ws_frame_t frame = { 0 };

    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "failed to read websocket frame header from fd=%d: %s", sockfd, esp_err_to_name(ret));
        return ret;
    }

    uint8_t *payload = calloc(1, frame.len + 1);
    if (payload == NULL) {
        ESP_LOGE(TAG, "failed to allocate websocket payload buffer: %zu bytes", frame.len + 1);
        return ESP_ERR_NO_MEM;
    }

    if (frame.len > 0) {
        frame.payload = payload;
        ret = httpd_ws_recv_frame(req, &frame, frame.len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "failed to read websocket payload from fd=%d: %s", sockfd, esp_err_to_name(ret));
            free(payload);
            return ret;
        }
    }

    switch (frame.type) {
    case HTTPD_WS_TYPE_TEXT:
        ESP_LOGI(TAG, "text frame fd=%d final=%d len=%zu payload=%.*s", sockfd, frame.final, frame.len, (int)frame.len, (char *)payload);
        break;
    case HTTPD_WS_TYPE_BINARY: {
        char preview[WS_SERVER_BINARY_PREVIEW_BYTES * 3 + 8];
        websocket_server_binary_preview(payload, frame.len, preview, sizeof(preview));
        ESP_LOGI(TAG, "binary frame fd=%d final=%d len=%zu preview=%s", sockfd, frame.final, frame.len, preview);
        break;
    }
    case HTTPD_WS_TYPE_CONTINUE:
        ESP_LOGW(TAG, "continuation frame fd=%d len=%zu not echoed", sockfd, frame.len);
        free(payload);
        return ESP_OK;
    default:
        ESP_LOGW(TAG, "unsupported websocket frame fd=%d type=%d len=%zu", sockfd, frame.type, frame.len);
        free(payload);
        return ESP_OK;
    }

    if (!frame.final) {
        ESP_LOGW(TAG, "fragmented websocket message fd=%d type=%d len=%zu ignored", sockfd, frame.type, frame.len);
        free(payload);
        return ESP_OK;
    }

    httpd_ws_frame_t reply = {
        .final = true,
        .fragmented = false,
        .type = frame.type,
        .payload = payload,
        .len = frame.len,
    };

    ret = httpd_ws_send_frame(req, &reply);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "failed to echo websocket frame to fd=%d: %s", sockfd, esp_err_to_name(ret));
    }

    free(payload);
    return ret;
}

esp_err_t websocket_server_run(void)
{
    if (CONFIG_ESPESP_WS_SERVER_PATH[0] == '\0' || CONFIG_ESPESP_WS_SERVER_PATH[0] != '/') {
        ESP_LOGE(TAG, "websocket path must start with '/'");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(wifi_station_connect());

    websocket_server_context_t ctx = {
        .server = NULL,
        .path = CONFIG_ESPESP_WS_SERVER_PATH,
        .auth_token = CONFIG_ESPESP_WS_SERVER_AUTH_TOKEN,
        .publish_period_ms = CONFIG_ESPESP_WS_SERVER_PUBLISH_PERIOD_MS,
        .max_clients = CONFIG_ESPESP_WS_SERVER_MAX_CLIENTS,
    };

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_ESPESP_WS_SERVER_PORT;
    config.max_open_sockets = CONFIG_ESPESP_WS_SERVER_MAX_CLIENTS + WS_SERVER_HTTPD_SOCKET_RESERVE;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = CONFIG_ESPESP_LAN_SERVICE_RECV_TIMEOUT_SEC;
    config.send_wait_timeout = CONFIG_ESPESP_LAN_SERVICE_SEND_TIMEOUT_SEC;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.close_fn = websocket_server_close_fn;
    config.global_user_ctx = &ctx;

    httpd_handle_t server = NULL;
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to start websocket server: %s", esp_err_to_name(ret));
        return ret;
    }

    ctx.server = server;

    const httpd_uri_t route = {
        .uri = ctx.path,
        .method = HTTP_GET,
        .handler = websocket_server_frame_handler,
        .user_ctx = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
        .ws_pre_handshake_cb = websocket_server_pre_handshake_cb,
        .ws_post_handshake_cb = websocket_server_post_handshake_cb,
    };

    ret = httpd_register_uri_handler(server, &route);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to register websocket route: %s", esp_err_to_name(ret));
        httpd_stop(server);
        return ret;
    }

    ESP_LOGI(TAG,
             "websocket server started on port %d, path=%s, max_clients=%zu, publish_period_ms=%" PRIu32,
             CONFIG_ESPESP_WS_SERVER_PORT,
             ctx.path,
             ctx.max_clients,
             ctx.publish_period_ms);
    if (ctx.auth_token != NULL && ctx.auth_token[0] != '\0') {
        ESP_LOGI(TAG, "websocket auth is enabled with Authorization: Bearer <token>");
    } else {
        ESP_LOGI(TAG, "websocket auth is disabled");
    }

    uint32_t sequence = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(ctx.publish_period_ms));

        if (ctx.server == NULL) {
            continue;
        }

        ret = websocket_server_broadcast_status(&ctx, sequence++);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "status broadcast failed: %s", esp_err_to_name(ret));
        }
    }

    return ESP_OK;
}
