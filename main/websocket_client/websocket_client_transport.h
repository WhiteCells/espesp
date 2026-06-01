#ifndef WEBSOCKET_CLIENT_TRANSPORT_H
#define WEBSOCKET_CLIENT_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_websocket_client.h"

bool websocket_client_uri_is_valid(const char *uri);
esp_err_t websocket_client_make_headers(char *headers, size_t headers_len);
void websocket_client_event_handler(void *handler_args,
                                    esp_event_base_t base,
                                    int32_t event_id,
                                    void *event_data);

#endif
