#include "led/led_blink.h"

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "led_blink";

esp_err_t led_blink_run(void)
{
    /* gpio_config 是配置板载 LED 或外接 LED GPIO 的常用入口。 */
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << CONFIG_CASE2_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    bool level = false;
    ESP_LOGI(TAG, "blink GPIO%d every %d ms", CONFIG_CASE2_LED_GPIO, CONFIG_CASE2_LED_PERIOD_MS);

    while (true) {
        level = !level;
        ESP_ERROR_CHECK(gpio_set_level(CONFIG_CASE2_LED_GPIO, level));
        ESP_LOGI(TAG, "GPIO%d level=%d", CONFIG_CASE2_LED_GPIO, level);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_CASE2_LED_PERIOD_MS));
    }

    return ESP_OK;
}
