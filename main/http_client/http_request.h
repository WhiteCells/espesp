#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <stdint.h>

#include "esp_err.h"
#include "esp_http_client.h"

typedef struct {
    const char *operation_name;
    const char *log_tag;
    const esp_http_client_config_t *client_config;
} http_request_t;

typedef struct {
    int status_code;
    int64_t content_length;
} http_response_meta_t;

esp_err_t http_request_perform(const http_request_t *request,
                               http_response_meta_t *response_meta);

#endif
