#ifndef LAN_SERVICE_H
#define LAN_SERVICE_H

#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "sdkconfig.h"

#ifdef CONFIG_ESPESP_LAN_SERVICE_REQUIRE_AUTH
#define ESPESP_LAN_SERVICE_REQUIRE_AUTH 1
#else
#define ESPESP_LAN_SERVICE_REQUIRE_AUTH 0
#endif

typedef struct {
    const char *service_name;
    const char *scheme;
    const char *auth_token;
    size_t max_body_len;
    bool require_auth;
} lan_service_config_t;

esp_err_t lan_service_register_handlers(httpd_handle_t server, const lan_service_config_t *config);

#endif
