#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#include "demos/demo_common.h"
#include "wifi_sta.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "wifi_sta";
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num;
static bool s_wifi_initialized;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_CASE2_WIFI_MAXIMUM_RETRY) {
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to WiFi (%d/%d)",
                     s_retry_num, CONFIG_CASE2_WIFI_MAXIMUM_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_sta_connect(void)
{
    if (strlen(CONFIG_CASE2_WIFI_SSID) == 0) {
        ESP_LOGE(TAG, "WiFi SSID is empty. Run `idf.py menuconfig` to set it.");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = demo_common_init_nvs();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = demo_common_init_netif();
    if (ret != ESP_OK) {
        return ret;
    }

    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_wifi_initialized) {
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ret = esp_wifi_init(&cfg);
        if (ret != ESP_OK) {
            return ret;
        }

        ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, NULL);
        if (ret != ESP_OK) {
            return ret;
        }

        ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL, NULL);
        if (ret != ESP_OK) {
            return ret;
        }

        s_wifi_initialized = true;
    }

    s_retry_num = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t wifi_config = { 0 };
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", CONFIG_CASE2_WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", CONFIG_CASE2_WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = strlen(CONFIG_CASE2_WIFI_PASSWORD) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to WiFi SSID: %s", CONFIG_CASE2_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "failed to connect");
    return ESP_FAIL;
}

esp_err_t wifi_sta_demo_run(void)
{
    esp_err_t ret = wifi_sta_connect();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi demo complete. The station is connected and IP is ready.");
    }

    return ret;
}
