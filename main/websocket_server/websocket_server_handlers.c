#include "websocket_server/websocket_server_handlers.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"
#include "websocket_server/websocket_server_messages.h"

#if CONFIG_HTTPD_WS_SUPPORT

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
        int written = snprintf(out + pos,
                               out_len - pos,
                               "%02X%s",
                               data[i],
                               i + 1 < preview_len ? " " : "");
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

static esp_err_t websocket_server_authorize_handshake(httpd_req_t *req,
                                                      const websocket_server_context_t *ctx)
{
    if (ctx == NULL || ctx->config.auth_token == NULL || ctx->config.auth_token[0] == '\0') {
        return ESP_OK;
    }

    char expected[WS_SERVER_AUTH_HEADER_MAX];
    int expected_len = snprintf(expected, sizeof(expected), "Bearer %s", ctx->config.auth_token);
    if (expected_len < 0 || expected_len >= (int)sizeof(expected)) {
        ESP_LOGE(WEBSOCKET_SERVER_TAG, "websocket auth token is too long");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "auth token too long");
        return ESP_FAIL;
    }

    char actual[WS_SERVER_AUTH_HEADER_MAX] = { 0 };
    if (httpd_req_get_hdr_value_str(req, "Authorization", actual, sizeof(actual)) != ESP_OK ||
        !secure_equals(actual, expected)) {
        ESP_LOGW(WEBSOCKET_SERVER_TAG, "reject websocket handshake from uri=%s due to auth", req->uri);
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "unauthorized");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t websocket_server_pre_handshake_cb(httpd_req_t *req)
{
    websocket_server_context_t *ctx = websocket_server_context_from_handle(req->handle);
    if (ctx == NULL) {
        ESP_LOGE(WEBSOCKET_SERVER_TAG, "missing websocket context");
        return ESP_FAIL;
    }

    esp_err_t ret = websocket_server_authorize_handshake(req, ctx);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t websocket_count = websocket_server_active_client_count(req->handle);
    if (websocket_count >= ctx->config.max_clients) {
        ESP_LOGW(WEBSOCKET_SERVER_TAG,
                 "reject websocket handshake from uri=%s due to max clients: %zu >= %zu",
                 req->uri,
                 websocket_count,
                 ctx->config.max_clients);
        httpd_resp_send_custom_err(req, "503 Service Unavailable", "too many websocket clients");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t websocket_server_post_handshake_cb(httpd_req_t *req)
{
    websocket_server_context_t *ctx = websocket_server_context_from_handle(req->handle);
    if (ctx == NULL) {
        return ESP_FAIL;
    }

    return websocket_server_send_welcome(req, ctx);
}

void websocket_server_close_session(httpd_handle_t hd, int sockfd)
{
    httpd_ws_client_info_t info = httpd_ws_get_fd_info(hd, sockfd);

    switch (info) {
    case HTTPD_WS_CLIENT_WEBSOCKET:
        ESP_LOGI(WEBSOCKET_SERVER_TAG, "websocket disconnected fd=%d", sockfd);
        break;
    case HTTPD_WS_CLIENT_HTTP:
        ESP_LOGD(WEBSOCKET_SERVER_TAG, "http session closed fd=%d", sockfd);
        break;
    default:
        ESP_LOGD(WEBSOCKET_SERVER_TAG, "session closed fd=%d", sockfd);
        break;
    }
}

static void websocket_server_log_received_frame(const httpd_ws_frame_t *frame, int sockfd)
{
    switch (frame->type) {
    case HTTPD_WS_TYPE_TEXT:
        ESP_LOGI(WEBSOCKET_SERVER_TAG,
                 "text frame fd=%d final=%d len=%zu payload=%.*s",
                 sockfd,
                 frame->final,
                 frame->len,
                 (int)frame->len,
                 (char *)frame->payload);
        break;
    case HTTPD_WS_TYPE_BINARY: {
        char preview[WS_SERVER_BINARY_PREVIEW_BYTES * 3 + 8];
        websocket_server_binary_preview(frame->payload, frame->len, preview, sizeof(preview));
        ESP_LOGI(WEBSOCKET_SERVER_TAG,
                 "binary frame fd=%d final=%d len=%zu preview=%s",
                 sockfd,
                 frame->final,
                 frame->len,
                 preview);
        break;
    }
    case HTTPD_WS_TYPE_CONTINUE:
        ESP_LOGW(WEBSOCKET_SERVER_TAG,
                 "continuation frame fd=%d len=%zu not echoed",
                 sockfd,
                 frame->len);
        break;
    default:
        ESP_LOGW(WEBSOCKET_SERVER_TAG,
                 "unsupported websocket frame fd=%d type=%d len=%zu",
                 sockfd,
                 frame->type,
                 frame->len);
        break;
    }
}

static esp_err_t websocket_server_receive_frame(httpd_req_t *req,
                                                httpd_ws_frame_t *frame,
                                                uint8_t **payload_out)
{
    if (req == NULL || frame == NULL || payload_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int sockfd = httpd_req_to_sockfd(req);
    *payload_out = NULL;

    esp_err_t ret = httpd_ws_recv_frame(req, frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(WEBSOCKET_SERVER_TAG,
                 "failed to read websocket frame header from fd=%d: %s",
                 sockfd,
                 esp_err_to_name(ret));
        return ret;
    }

    if (frame->len == SIZE_MAX) {
        ESP_LOGE(WEBSOCKET_SERVER_TAG, "websocket frame from fd=%d is too large", sockfd);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *payload = calloc(1, frame->len + 1);
    if (payload == NULL) {
        ESP_LOGE(WEBSOCKET_SERVER_TAG,
                 "failed to allocate websocket payload buffer: %zu bytes",
                 frame->len + 1);
        return ESP_ERR_NO_MEM;
    }

    frame->payload = payload;
    if (frame->len > 0) {
        ret = httpd_ws_recv_frame(req, frame, frame->len);
        if (ret != ESP_OK) {
            ESP_LOGW(WEBSOCKET_SERVER_TAG,
                     "failed to read websocket payload from fd=%d: %s",
                     sockfd,
                     esp_err_to_name(ret));
            free(payload);
            return ret;
        }
    }

    *payload_out = payload;
    return ESP_OK;
}

static esp_err_t websocket_server_echo_frame(httpd_req_t *req, const httpd_ws_frame_t *frame)
{
    httpd_ws_frame_t reply = {
        .final = true,
        .fragmented = false,
        .type = frame->type,
        .payload = frame->payload,
        .len = frame->len,
    };

    esp_err_t ret = httpd_ws_send_frame(req, &reply);
    if (ret != ESP_OK) {
        ESP_LOGW(WEBSOCKET_SERVER_TAG,
                 "failed to echo websocket frame to fd=%d: %s",
                 httpd_req_to_sockfd(req),
                 esp_err_to_name(ret));
    }

    return ret;
}

static esp_err_t websocket_server_frame_handler(httpd_req_t *req)
{
    int sockfd = httpd_req_to_sockfd(req);
    httpd_ws_frame_t frame = { 0 };
    uint8_t *payload = NULL;

    esp_err_t ret = websocket_server_receive_frame(req, &frame, &payload);
    if (ret != ESP_OK) {
        return ret;
    }

    websocket_server_log_received_frame(&frame, sockfd);

    if (frame.type == HTTPD_WS_TYPE_CONTINUE) {
        free(payload);
        return ESP_OK;
    }

    if (frame.type != HTTPD_WS_TYPE_TEXT && frame.type != HTTPD_WS_TYPE_BINARY) {
        free(payload);
        return ESP_OK;
    }

    if (!frame.final) {
        ESP_LOGW(WEBSOCKET_SERVER_TAG,
                 "fragmented websocket message fd=%d type=%d len=%zu ignored",
                 sockfd,
                 frame.type,
                 frame.len);
        free(payload);
        return ESP_OK;
    }

    ret = websocket_server_echo_frame(req, &frame);
    free(payload);
    return ret;
}

esp_err_t websocket_server_register_handlers(httpd_handle_t server, const websocket_server_context_t *ctx)
{
    if (server == NULL || ctx == NULL || ctx->config.path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const httpd_uri_t route = {
        .uri = ctx->config.path,
        .method = HTTP_GET,
        .handler = websocket_server_frame_handler,
        .user_ctx = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL,
        .ws_pre_handshake_cb = websocket_server_pre_handshake_cb,
        .ws_post_handshake_cb = websocket_server_post_handshake_cb,
    };

    return httpd_register_uri_handler(server, &route);
}

#endif
