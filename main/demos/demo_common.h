#ifndef DEMO_COMMON_H
#define DEMO_COMMON_H

#include <stdbool.h>

#include "esp_err.h"

typedef esp_err_t (*demo_runner_t)(void);

typedef struct {
    const char *key;
    const char *title;
    const char *doc_path;
    demo_runner_t run;
    bool needs_wifi;
    bool loops_forever;
} demo_case_t;

esp_err_t demo_common_init_nvs(void);
esp_err_t demo_common_init_netif(void);
void demo_common_print_banner(const demo_case_t *demo);
void demo_common_idle_forever(void);

#endif
