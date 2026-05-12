#include "system_info/system_info.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "sdkconfig.h"

static const char *TAG = "system_info";

static void print_chip_features(uint32_t features)
{
    printf("features:");
    printf("%s", (features & CHIP_FEATURE_WIFI_BGN) ? " Wi-Fi" : "");
    printf("%s", (features & CHIP_FEATURE_BT) ? " BT" : "");
    printf("%s", (features & CHIP_FEATURE_BLE) ? " BLE" : "");
    printf("%s", (features & CHIP_FEATURE_IEEE802154) ? " 802.15.4" : "");
    printf("%s", (features & CHIP_FEATURE_EMB_FLASH) ? " embedded-flash" : " external-flash");
    printf("\n");
}

esp_err_t system_info_run(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;
    uint8_t mac[6] = { 0 };
    const esp_app_desc_t *app = esp_app_get_description();

    esp_chip_info(&chip_info);
    /* 固件描述来自 app image header，常用于确认版本和构建来源。 */
    esp_err_t ret = esp_flash_get_size(NULL, &flash_size);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        return ret;
    }

    printf("project: %s\n", app->project_name);
    printf("app version: %s\n", app->version);
    printf("idf version: %s\n", app->idf_ver);
    printf("target: %s\n", CONFIG_IDF_TARGET);
    printf("cpu cores: %d\n", chip_info.cores);
    printf("silicon revision: v%d.%d\n", chip_info.revision / 100, chip_info.revision % 100);
    print_chip_features(chip_info.features);
    printf("flash size: %" PRIu32 " MB\n", flash_size / (1024 * 1024));
    printf("free heap: %" PRIu32 " bytes\n", esp_get_free_heap_size());
    printf("minimum free heap since boot: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());
    printf("largest DMA-capable block: %zu bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    printf("Wi-Fi STA MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "系统信息模块完成，适合先理解芯片、固件和堆内存状态。");
    return ESP_OK;
}
