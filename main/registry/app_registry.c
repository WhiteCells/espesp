#include "registry/app_registry.h"

#include "sdkconfig.h"

#include "adc_reader/adc_reader.h"
#include "display/display.h"
#include "http_client/http_get.h"
#include "i2c_scan/i2c_scan.h"
#include "led/led_blink.h"
#include "microphone/microphone.h"
#include "mqtt_client_demo/mqtt_client_demo.h"
#include "nvs_counter/nvs_counter.h"
#include "rtos_tasks/rtos_tasks.h"
#include "speaker/speaker.h"
#include "system_info/system_info.h"
#include "uart_echo/uart_echo.h"
#include "wifi_station/wifi_station.h"

/* The registry keeps app_main small and makes each feature folder independent. */
static const app_case_t s_cases[] = {
    {
        .key = "system_info",
        .title = "系统信息与启动流程",
        .doc_path = "docs/modules/01_system_info.md",
        .run = system_info_run,
        .needs_wifi = false,
        .runs_forever = false,
    },
    {
        .key = "rtos_tasks",
        .title = "FreeRTOS 任务与队列",
        .doc_path = "docs/modules/02_rtos_tasks.md",
        .run = rtos_tasks_run,
        .needs_wifi = false,
        .runs_forever = true,
    },
    {
        .key = "led_blink",
        .title = "LED GPIO 输出",
        .doc_path = "docs/modules/03_led_blink.md",
        .run = led_blink_run,
        .needs_wifi = false,
        .runs_forever = true,
    },
    {
        .key = "nvs_counter",
        .title = "NVS 键值存储",
        .doc_path = "docs/modules/04_nvs_counter.md",
        .run = nvs_counter_run,
        .needs_wifi = false,
        .runs_forever = false,
    },
    {
        .key = "wifi_station",
        .title = "Wi-Fi STA 入网",
        .doc_path = "docs/modules/05_wifi_station.md",
        .run = wifi_station_run,
        .needs_wifi = true,
        .runs_forever = false,
    },
    {
        .key = "http_get",
        .title = "HTTP Client GET 请求",
        .doc_path = "docs/modules/06_http_get.md",
        .run = http_get_run,
        .needs_wifi = true,
        .runs_forever = false,
    },
    {
        .key = "adc_reader",
        .title = "ADC 单次采样",
        .doc_path = "docs/modules/07_adc_reader.md",
        .run = adc_reader_run,
        .needs_wifi = false,
        .runs_forever = true,
    },
    {
        .key = "uart_echo",
        .title = "UART 回显",
        .doc_path = "docs/modules/08_uart_echo.md",
        .run = uart_echo_run,
        .needs_wifi = false,
        .runs_forever = true,
    },
    {
        .key = "i2c_scan",
        .title = "I2C 总线扫描",
        .doc_path = "docs/modules/09_i2c_scan.md",
        .run = i2c_scan_run,
        .needs_wifi = false,
        .runs_forever = false,
    },
    {
        .key = "microphone",
        .title = "I2S 麦克风采样",
        .doc_path = "docs/modules/10_microphone.md",
        .run = microphone_run,
        .needs_wifi = false,
        .runs_forever = true,
    },
    {
        .key = "speaker",
        .title = "I2S 扬声器输出",
        .doc_path = "docs/modules/11_speaker.md",
        .run = speaker_run,
        .needs_wifi = false,
        .runs_forever = true,
    },
    {
        .key = "display",
        .title = "I2C OLED 显示屏",
        .doc_path = "docs/modules/12_display.md",
        .run = display_run,
        .needs_wifi = false,
        .runs_forever = false,
    },
    {
        .key = "mqtt_client",
        .title = "MQTT 发布订阅",
        .doc_path = "docs/modules/13_mqtt_client.md",
        .run = mqtt_client_demo_run,
        .needs_wifi = true,
        .runs_forever = true,
    },
};

const app_case_t *app_registry_all(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(s_cases) / sizeof(s_cases[0]);
    }

    return s_cases;
}

const app_case_t *app_registry_selected(void)
{
#if CONFIG_CASE2_MODULE_SYSTEM_INFO
    return &s_cases[0];
#elif CONFIG_CASE2_MODULE_RTOS_TASKS
    return &s_cases[1];
#elif CONFIG_CASE2_MODULE_LED_BLINK
    return &s_cases[2];
#elif CONFIG_CASE2_MODULE_NVS_COUNTER
    return &s_cases[3];
#elif CONFIG_CASE2_MODULE_WIFI_STATION
    return &s_cases[4];
#elif CONFIG_CASE2_MODULE_HTTP_GET
    return &s_cases[5];
#elif CONFIG_CASE2_MODULE_ADC_READER
    return &s_cases[6];
#elif CONFIG_CASE2_MODULE_UART_ECHO
    return &s_cases[7];
#elif CONFIG_CASE2_MODULE_I2C_SCAN
    return &s_cases[8];
#elif CONFIG_CASE2_MODULE_MICROPHONE
    return &s_cases[9];
#elif CONFIG_CASE2_MODULE_SPEAKER
    return &s_cases[10];
#elif CONFIG_CASE2_MODULE_DISPLAY
    return &s_cases[11];
#elif CONFIG_CASE2_MODULE_MQTT_CLIENT
    return &s_cases[12];
#else
    return &s_cases[0];
#endif
}
