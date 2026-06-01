#ifndef HTTP_SERVER_RUNTIME_H
#define HTTP_SERVER_RUNTIME_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "lan_service/lan_service.h"

typedef struct {
    httpd_handle_t handle;
    httpd_config_t httpd_config;
    lan_service_config_t service_config;
} http_server_runtime_t;

esp_err_t http_server_runtime_prepare(http_server_runtime_t *runtime);
esp_err_t http_server_runtime_start(http_server_runtime_t *runtime);
void http_server_runtime_log_startup(const http_server_runtime_t *runtime);
void http_server_runtime_wait_forever(void);

#endif
