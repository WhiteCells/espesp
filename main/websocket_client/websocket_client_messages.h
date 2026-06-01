#ifndef WEBSOCKET_CLIENT_MESSAGES_H
#define WEBSOCKET_CLIENT_MESSAGES_H

#include <stdint.h>

#include "esp_err.h"
#include "esp_websocket_client.h"
#include "websocket_client/websocket_client_context.h"

esp_err_t websocket_client_send_initial_payload(esp_websocket_client_handle_t client);
esp_err_t websocket_client_send_status(esp_websocket_client_handle_t client, uint32_t sequence);
void websocket_client_log_incoming_data(const esp_websocket_event_data_t *data,
                                        websocket_client_context_t *ctx);

#endif
