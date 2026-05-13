#include "core/app_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "app_common";
static bool s_nvs_ready;
static bool s_netif_ready;

esp_err_t app_common_init_nvs(void)
{
    if (s_nvs_ready) {
        return ESP_OK;
    }

    /* NVS 是 Wi-Fi、设备配置和运行计数最常用的轻量级持久化入口。 */
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

esp_err_t app_common_init_netif(void)
{
    if (s_netif_ready) {
        return ESP_OK;
    }

    /* 网络模块复用同一个 esp_netif 与默认事件循环，避免重复初始化。 */
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

void app_common_print_banner(const app_case_t *selected_case)
{
    printf("\n");
    printf("============================================================\n");
    printf("module: %s (%s)\n", selected_case->title, selected_case->key);
    printf("配套文档: %s\n", selected_case->doc_path);
    printf("切换模块: idf.py menuconfig -> ESPESP Menu -> Module selector\n");
    printf("============================================================\n");
}

void app_common_idle_forever(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
