#include "websocket_server/websocket_server_messages.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#if CONFIG_HTTPD_WS_SUPPORT

static int websocket_server_format_status_payload(char *payload,
                                                  size_t payload_len,
                                                  const websocket_server_context_t *ctx,
                                                  uint32_t sequence,
                                                  size_t websocket_count)
{
    return snprintf(payload,
                    payload_len,
                    "{\"type\":\"status\",\"service\":\"websocket_server\",\"path\":\"%s\",\"seq\":%" PRIu32
                    ",\"clients\":%zu,\"free_heap\":%" PRIu32 ",\"uptime_ms\":%" PRId64 "}",
                    ctx->config.path,
                    sequence,
                    websocket_count,
                    esp_get_free_heap_size(),
                    esp_timer_get_time() / 1000);
}

static int websocket_server_format_welcome_payload(char *payload,
                                                   size_t payload_len,
                                                   const websocket_server_context_t *ctx,
                                                   size_t websocket_count)
{
    return snprintf(payload,
                    payload_len,
                    "{\"type\":\"hello\",\"service\":\"websocket_server\",\"path\":\"%s\",\"clients\":%zu,"
                    "\"free_heap\":%" PRIu32 ",\"uptime_ms\":%" PRId64 "}",
                    ctx->config.path,
                    websocket_count,
                    esp_get_free_heap_size(),
                    esp_timer_get_time() / 1000);
}

esp_err_t websocket_server_snapshot_clients(httpd_handle_t handle,
                                            websocket_server_client_snapshot_t *snapshot)
{
    if (handle == NULL || snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    snapshot->fd_count = sizeof(snapshot->fds) / sizeof(snapshot->fds[0]);
    snapshot->websocket_count = 0;

    esp_err_t ret = httpd_get_client_list(handle, &snapshot->fd_count, snapshot->fds);
    if (ret != ESP_OK) {
        return ret;
    }

    for (size_t i = 0; i < snapshot->fd_count; i++) {
        if (httpd_ws_get_fd_info(handle, snapshot->fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            snapshot->websocket_count++;
        }
    }

    return ESP_OK;
}

size_t websocket_server_active_client_count(httpd_handle_t handle)
{
    websocket_server_client_snapshot_t snapshot = { 0 };
    esp_err_t ret = websocket_server_snapshot_clients(handle, &snapshot);
    if (ret != ESP_OK) {
        ESP_LOGW(WEBSOCKET_SERVER_TAG, "failed to list websocket clients: %s", esp_err_to_name(ret));
        return 0;
    }

    return snapshot.websocket_count;
}

esp_err_t websocket_server_send_welcome(httpd_req_t *req, const websocket_server_context_t *ctx)
{
    if (req == NULL || ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int sockfd = httpd_req_to_sockfd(req);
    size_t websocket_count = websocket_server_active_client_count(req->handle);

    char payload[WS_SERVER_JSON_PAYLOAD_MAX];
    int written = websocket_server_format_welcome_payload(payload, sizeof(payload), ctx, websocket_count);
    if (written < 0 || written >= (int)sizeof(payload)) {
        ESP_LOGW(WEBSOCKET_SERVER_TAG, "welcome payload too large");
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
        ESP_LOGW(WEBSOCKET_SERVER_TAG,
                 "failed to send welcome frame to fd=%d: %s",
                 sockfd,
                 esp_err_to_name(ret));
    } else {
        ESP_LOGI(WEBSOCKET_SERVER_TAG, "websocket connected fd=%d path=%s", sockfd, ctx->config.path);
    }

    return ESP_OK;
}

esp_err_t websocket_server_broadcast_status(const websocket_server_context_t *ctx, uint32_t sequence)
{
    if (ctx == NULL || ctx->server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    websocket_server_client_snapshot_t snapshot = { 0 };
    esp_err_t ret = websocket_server_snapshot_clients(ctx->server, &snapshot);
    if (ret != ESP_OK) {
        return ret;
    }

    if (snapshot.websocket_count == 0) {
        ESP_LOGD(WEBSOCKET_SERVER_TAG,
                 "status tick seq=%" PRIu32 " skipped: no websocket clients",
                 sequence);
        return ESP_OK;
    }

    char payload[WS_SERVER_JSON_PAYLOAD_MAX];
    int written = websocket_server_format_status_payload(payload,
                                                         sizeof(payload),
                                                         ctx,
                                                         sequence,
                                                         snapshot.websocket_count);
    if (written < 0 || written >= (int)sizeof(payload)) {
        ESP_LOGE(WEBSOCKET_SERVER_TAG, "status payload too large");
        return ESP_FAIL;
    }

    esp_err_t last_error = ESP_OK;
    for (size_t i = 0; i < snapshot.fd_count; i++) {
        int sockfd = snapshot.fds[i];
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
            ESP_LOGW(WEBSOCKET_SERVER_TAG,
                     "broadcast seq=%" PRIu32 " to fd=%d failed: %s",
                     sequence,
                     sockfd,
                     esp_err_to_name(ret));
            last_error = ret;
        }
    }

    return last_error;
}

#endif
