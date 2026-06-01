#ifndef HTTPS_CLIENT_SECURITY_H
#define HTTPS_CLIENT_SECURITY_H

#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_client.h"

typedef struct {
    char *trusted_cert_pem;
    bool skip_common_name_check;
    esp_err_t (*crt_bundle_attach)(void *conf);
} https_client_security_t;

esp_err_t https_client_security_prepare(https_client_security_t *security);
void https_client_security_apply(esp_http_client_config_t *client_config,
                                 const https_client_security_t *security);
void https_client_security_release(https_client_security_t *security);

#endif
