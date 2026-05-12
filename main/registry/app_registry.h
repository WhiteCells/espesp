#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#include <stddef.h>

#include "core/app_common.h"

const app_case_t *app_registry_selected(void);
const app_case_t *app_registry_all(size_t *count);

#endif
