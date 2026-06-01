#ifndef WEBSOCKET_SERVER_RUNTIME_H
#define WEBSOCKET_SERVER_RUNTIME_H

#include "esp_err.h"
#include "websocket_server/websocket_server_context.h"

esp_err_t websocket_server_runtime_prepare(websocket_server_runtime_t *runtime);
esp_err_t websocket_server_runtime_start(websocket_server_runtime_t *runtime);
void websocket_server_runtime_log_startup(const websocket_server_runtime_t *runtime);

#endif
