#ifndef SPEAKER_CLIENT_TRANSPORT_H
#define SPEAKER_CLIENT_TRANSPORT_H

#include <stdbool.h>

#include "esp_err.h"
#include "esp_event_base.h"
#include "esp_websocket_client.h"
#include "speaker_client/speaker_client_context.h"

bool speaker_client_uri_is_valid(const char *uri);
esp_err_t speaker_client_make_headers(char *headers, size_t headers_len);
void speaker_client_event_handler(void *handler_args,
                                  esp_event_base_t base,
                                  int32_t event_id,
                                  void *event_data);
esp_err_t speaker_client_send_status(esp_websocket_client_handle_t client,
                                     const speaker_client_context_t *ctx);

#endif
