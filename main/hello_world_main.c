#include "demos/demo_common.h"
#include "demos/demo_registry.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "app_main";

void app_main(void)
{
    const demo_case_t *demo = demo_registry_selected();

    demo_common_print_banner(demo);

    esp_err_t ret = demo->run();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "demo '%s' failed: %s", demo->key, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "demo '%s' finished or is running in its own task", demo->key);
    }

    if (!demo->loops_forever) {
        ESP_LOGI(TAG, "idle forever so you can keep reading serial logs");
        demo_common_idle_forever();
    }
}
