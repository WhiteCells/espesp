#include "rtos_tasks/rtos_tasks.h"

#include <inttypes.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"

typedef struct {
    uint32_t sequence;
    uint32_t uptime_ms;
} sensor_sample_t;

static const char *TAG = "rtos_tasks";

static QueueHandle_t s_sample_queue;

static void producer_task(void *arg)
{
    uint32_t sequence = 0;

    while (true) {
        /* 用 uptime 模拟传感器数据，重点观察队列传递和任务调度。 */
        sensor_sample_t sample = {
            .sequence = sequence++,
            .uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };

        if (xQueueSend(s_sample_queue, &sample, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "queue full, drop sample %" PRIu32, sample.sequence);
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_ESPESP_FREERTOS_PRODUCER_PERIOD_MS));
    }
}

static void consumer_task(void *arg)
{
    sensor_sample_t sample;

    while (true) {
        if (xQueueReceive(s_sample_queue, &sample, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "consume sample=%" PRIu32 ", uptime=%" PRIu32 " ms, free_stack=%u",
                     sample.sequence,
                     sample.uptime_ms,
                     uxTaskGetStackHighWaterMark(NULL));
        }
    }
}

static void heartbeat_task(void *arg)
{
    while (true) {
        ESP_LOGI(TAG, "heartbeat: tasks are alive, free heap=%" PRIu32,
                 esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

esp_err_t rtos_tasks_run(void)
{
    s_sample_queue = xQueueCreate(CONFIG_ESPESP_FREERTOS_QUEUE_LENGTH, sizeof(sensor_sample_t));
    if (s_sample_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(producer_task, "producer", 3072, NULL, 5, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ok = xTaskCreate(consumer_task, "consumer", 3072, NULL, 5, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ok = xTaskCreate(heartbeat_task, "heartbeat", 3072, NULL, 3, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "FreeRTOS module started: producer -> queue -> consumer");
    return ESP_OK;
}
