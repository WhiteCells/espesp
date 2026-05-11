#include "demos/demo_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "demo_common";
static bool s_nvs_ready;
static bool s_netif_ready;

esp_err_t demo_common_init_nvs(void)
{
    if (s_nvs_ready) {
        return ESP_OK;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase before init");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret == ESP_OK) {
        s_nvs_ready = true;
    }

    return ret;
}

esp_err_t demo_common_init_netif(void)
{
    if (s_netif_ready) {
        return ESP_OK;
    }

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    s_netif_ready = true;
    return ESP_OK;
}

void demo_common_print_banner(const demo_case_t *demo)
{
    printf("\n");
    printf("============================================================\n");
    printf("demo: %s (%s)\n", demo->title, demo->key);
    printf("配套文档: %s\n", demo->doc_path);
    printf("切换 demo: idf.py menuconfig -> Case2 ESP Learning -> Demo selector\n");
    printf("============================================================\n");
}

void demo_common_idle_forever(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
