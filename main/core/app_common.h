#ifndef APP_COMMON_H
#define APP_COMMON_H

#include <stdbool.h>

#include "esp_err.h"

typedef esp_err_t (*app_case_runner_t)(void);

typedef struct {
    const char *key;
    const char *title;
    const char *doc_path;
    app_case_runner_t run;
    bool needs_wifi;
    bool runs_forever;
} app_case_t;

esp_err_t app_common_init_nvs(void);
esp_err_t app_common_init_netif(void);
void app_common_print_banner(const app_case_t *selected_case);
void app_common_idle_forever(void);

#endif
