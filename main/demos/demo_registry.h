#ifndef DEMO_REGISTRY_H
#define DEMO_REGISTRY_H

#include <stddef.h>

#include "demos/demo_common.h"

const demo_case_t *demo_registry_selected(void);
const demo_case_t *demo_registry_all(size_t *count);

#endif
