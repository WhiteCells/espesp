#ifndef HTTP_RESPONSE_LOG_H
#define HTTP_RESPONSE_LOG_H

#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_client.h"

typedef struct {
    const char *tag;
    int printed_bytes;
    int print_limit;
    bool limit_reached;
} http_response_log_t;

void http_response_log_init(http_response_log_t *response_log,
                            const char *tag,
                            int print_limit);
esp_err_t http_response_log_event_handler(esp_http_client_event_t *evt);
void http_response_log_print_summary(const http_response_log_t *response_log);

#endif
