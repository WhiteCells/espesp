#ifndef HTTPS_SERVER_RUNTIME_H
#define HTTPS_SERVER_RUNTIME_H

#include "esp_err.h"
#include "esp_https_server.h"
#include "lan_service/lan_service.h"

typedef struct {
    httpd_handle_t handle;
    httpd_ssl_config_t httpd_config;
    lan_service_config_t service_config;
} https_server_runtime_t;

esp_err_t https_server_runtime_prepare(https_server_runtime_t *runtime);
esp_err_t https_server_runtime_start(https_server_runtime_t *runtime);
void https_server_runtime_log_startup(const https_server_runtime_t *runtime);
void https_server_runtime_wait_forever(void);

#endif
