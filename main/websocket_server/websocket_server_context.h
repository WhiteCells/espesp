#ifndef WEBSOCKET_SERVER_CONTEXT_H
#define WEBSOCKET_SERVER_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#include "esp_http_server.h"

#define WS_SERVER_HANDSHAKE_SOCKET_RESERVE 2
#define WS_SERVER_AUTH_HEADER_MAX 256
#define WS_SERVER_BINARY_PREVIEW_BYTES 16
#define WS_SERVER_JSON_PAYLOAD_MAX 256

typedef struct {
    const char *path;
    const char *auth_token;
    uint32_t publish_period_ms;
    size_t max_clients;
} websocket_server_config_t;

typedef struct {
    httpd_handle_t server;
    websocket_server_config_t config;
} websocket_server_context_t;

typedef struct {
    httpd_handle_t handle;
    httpd_config_t httpd_config;
    websocket_server_config_t server_config;
    websocket_server_context_t *context;
} websocket_server_runtime_t;

extern const char *WEBSOCKET_SERVER_TAG;

static inline websocket_server_context_t *websocket_server_context_from_handle(httpd_handle_t handle)
{
    return (websocket_server_context_t *)httpd_get_global_user_ctx(handle);
}

#endif
