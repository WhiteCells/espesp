#ifndef WEBSOCKET_SERVER_MESSAGES_H
#define WEBSOCKET_SERVER_MESSAGES_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdkconfig.h"
#include "websocket_server/websocket_server_context.h"

typedef struct {
    int fds[CONFIG_ESPESP_WS_SERVER_MAX_CLIENTS + WS_SERVER_HANDSHAKE_SOCKET_RESERVE];
    size_t fd_count;
    size_t websocket_count;
} websocket_server_client_snapshot_t;

size_t websocket_server_active_client_count(httpd_handle_t handle);
esp_err_t websocket_server_snapshot_clients(httpd_handle_t handle,
                                            websocket_server_client_snapshot_t *snapshot);
esp_err_t websocket_server_send_welcome(httpd_req_t *req, const websocket_server_context_t *ctx);
esp_err_t websocket_server_broadcast_status(const websocket_server_context_t *ctx, uint32_t sequence);

#endif
