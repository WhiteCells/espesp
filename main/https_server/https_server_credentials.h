#ifndef HTTPS_SERVER_CREDENTIALS_H
#define HTTPS_SERVER_CREDENTIALS_H

#include "esp_err.h"

typedef struct {
    char *servercert;
    char *private_key;
} https_server_credentials_t;

esp_err_t https_server_credentials_load(https_server_credentials_t *credentials);
esp_err_t https_server_credentials_validate(const https_server_credentials_t *credentials);
void https_server_credentials_release(https_server_credentials_t *credentials);

#endif
