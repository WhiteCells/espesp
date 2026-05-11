#include "demos/nvs_counter_demo.h"

#include <inttypes.h>

#include "demos/demo_common.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "nvs_counter";
static const char *NVS_NAMESPACE = "learn";
static const char *BOOT_KEY = "boot_count";

esp_err_t nvs_counter_demo_run(void)
{
    esp_err_t ret = demo_common_init_nvs();
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_handle_t handle;
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    uint32_t boot_count = 0;
    ret = nvs_get_u32(handle, BOOT_KEY, &boot_count);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "first boot recorded by this demo");
        ret = ESP_OK;
    }

    if (ret == ESP_OK) {
        boot_count++;
        ESP_LOGI(TAG, "boot_count=%" PRIu32, boot_count);
        ESP_ERROR_CHECK(nvs_set_u32(handle, BOOT_KEY, boot_count));
        ESP_ERROR_CHECK(nvs_commit(handle));
        ESP_LOGI(TAG, "stored boot_count. Press reset and run again to see it increase.");
    }

    nvs_close(handle);
    return ret;
}
