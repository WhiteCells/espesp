#include "core/app_common.h"
#include "registry/app_registry.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "app_main";

void app_main(void)
{
    const app_case_t *selected_case = app_registry_selected();
    app_common_print_banner(selected_case);

    esp_err_t ret = selected_case->run();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "module '%s' failed: %s", selected_case->key, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "module '%s' finished or is running in its own task", selected_case->key);
    }

    if (!selected_case->runs_forever) {
        ESP_LOGI(TAG, "idle forever so you can keep reading serial logs");
        app_common_idle_forever();
    }
}
