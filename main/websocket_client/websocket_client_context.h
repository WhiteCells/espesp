#ifndef WEBSOCKET_CLIENT_CONTEXT_H
#define WEBSOCKET_CLIENT_CONTEXT_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WEBSOCKET_CLIENT_CONNECTED_BIT BIT0
#define WEBSOCKET_CLIENT_ERROR_BIT BIT1
#define WEBSOCKET_CLIENT_AUTH_HEADER_MAX 256
#define WEBSOCKET_CLIENT_BINARY_PREVIEW_BYTES 16
#define WEBSOCKET_CLIENT_STATUS_PAYLOAD_MAX 192

typedef struct {
    EventGroupHandle_t event_group;
    uint32_t received_frames;
} websocket_client_context_t;

extern const char *WEBSOCKET_CLIENT_TAG;

#endif
