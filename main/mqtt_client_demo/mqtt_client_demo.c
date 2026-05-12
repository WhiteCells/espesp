#include "mqtt_client_demo/mqtt_client_demo.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "wifi_station/wifi_station.h"

#define MQTT_CONNECTED_BIT BIT0
#define MQTT_ERROR_BIT BIT1

typedef struct {
    EventGroupHandle_t event_group;
} mqtt_client_demo_context_t;

static const char *TAG = "mqtt_client";

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    mqtt_client_demo_context_t *ctx = (mqtt_client_demo_context_t *)handler_args;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;

    ESP_LOGD(TAG, "event base=%s, id=%" PRIi32, base, event_id);

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "connected to broker");
        xEventGroupSetBits(ctx->event_group, MQTT_CONNECTED_BIT);

        int msg_id = esp_mqtt_client_subscribe(client, CONFIG_CASE2_MQTT_CMD_TOPIC, 0);
        ESP_LOGI(TAG, "subscribe topic=%s, msg_id=%d", CONFIG_CASE2_MQTT_CMD_TOPIC, msg_id);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected from broker");
        xEventGroupClearBits(ctx->event_group, MQTT_CONNECTED_BIT);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "subscribed, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "published, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG,
                 "received topic=%.*s data=%.*s",
                 event->topic_len,
                 event->topic,
                 event->data_len,
                 event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        xEventGroupSetBits(ctx->event_group, MQTT_ERROR_BIT);
        if (event->error_handle != NULL) {
            ESP_LOGE(TAG, "error type=%d", event->error_handle->error_type);
        }
        break;
    default:
        ESP_LOGD(TAG, "other MQTT event id=%" PRIi32, event_id);
        break;
    }
}

static esp_err_t mqtt_publish_text(esp_mqtt_client_handle_t client,
                                   const char *topic,
                                   const char *payload)
{
    int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 0, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "publish failed: topic=%s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "publish topic=%s, payload=%s, msg_id=%d", topic, payload, msg_id);
    return ESP_OK;
}

esp_err_t mqtt_client_demo_run(void)
{
    if (strlen(CONFIG_CASE2_MQTT_BROKER_URI) == 0) {
        ESP_LOGE(TAG, "MQTT broker URI is empty. Run `idf.py menuconfig` to set it.");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(wifi_station_connect());

    mqtt_client_demo_context_t ctx = {
        .event_group = xEventGroupCreate(),
    };
    if (ctx.event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_CASE2_MQTT_BROKER_URI,
        .credentials.client_id = CONFIG_CASE2_MQTT_CLIENT_ID,
        .session.keepalive = CONFIG_CASE2_MQTT_KEEPALIVE_SEC,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        vEventGroupDelete(ctx.event_group);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_mqtt_client_register_event(client,
                                                   ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler,
                                                   &ctx);
    if (ret != ESP_OK) {
        esp_mqtt_client_destroy(client);
        vEventGroupDelete(ctx.event_group);
        return ret;
    }

    ESP_LOGI(TAG, "connect broker=%s, client_id=%s", CONFIG_CASE2_MQTT_BROKER_URI, CONFIG_CASE2_MQTT_CLIENT_ID);
    ret = esp_mqtt_client_start(client);
    if (ret != ESP_OK) {
        esp_mqtt_client_destroy(client);
        vEventGroupDelete(ctx.event_group);
        return ret;
    }

    EventBits_t bits = xEventGroupWaitBits(ctx.event_group,
                                           MQTT_CONNECTED_BIT | MQTT_ERROR_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(CONFIG_CASE2_MQTT_CONNECT_TIMEOUT_MS));
    if ((bits & MQTT_CONNECTED_BIT) == 0) {
        ESP_LOGE(TAG, "MQTT connect timeout or error");
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        vEventGroupDelete(ctx.event_group);
        return (bits & MQTT_ERROR_BIT) ? ESP_FAIL : ESP_ERR_TIMEOUT;
    }

    ESP_ERROR_CHECK(mqtt_publish_text(client, CONFIG_CASE2_MQTT_STATUS_TOPIC, "online"));

    uint32_t sequence = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_CASE2_MQTT_PUBLISH_PERIOD_MS));

        bits = xEventGroupGetBits(ctx.event_group);
        if ((bits & MQTT_CONNECTED_BIT) == 0) {
            ESP_LOGW(TAG, "waiting for MQTT reconnect");
            continue;
        }

        char payload[160];
        snprintf(payload,
                 sizeof(payload),
                 "{\"client\":\"%s\",\"seq\":%" PRIu32 ",\"free_heap\":%" PRIu32 "}",
                 CONFIG_CASE2_MQTT_CLIENT_ID,
                 sequence++,
                 esp_get_free_heap_size());
        ESP_ERROR_CHECK(mqtt_publish_text(client, CONFIG_CASE2_MQTT_STATUS_TOPIC, payload));
    }

    return ESP_OK;
}
