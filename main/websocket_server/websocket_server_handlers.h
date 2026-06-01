#ifndef WEBSOCKET_SERVER_HANDLERS_H
#define WEBSOCKET_SERVER_HANDLERS_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "websocket_server/websocket_server_context.h"

esp_err_t websocket_server_register_handlers(httpd_handle_t server, const websocket_server_context_t *ctx);
void websocket_server_close_session(httpd_handle_t hd, int sockfd);

#endif
