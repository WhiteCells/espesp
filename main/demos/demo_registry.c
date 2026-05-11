#include "demos/demo_registry.h"

#include "sdkconfig.h"

#include "demos/adc_oneshot_demo.h"
#include "demos/freertos_tasks_demo.h"
#include "demos/gpio_blink_demo.h"
#include "demos/http_get_demo.h"
#include "demos/i2c_scan_demo.h"
#include "demos/nvs_counter_demo.h"
#include "demos/system_info_demo.h"
#include "demos/uart_echo_demo.h"
#include "wifi_sta.h"

static const demo_case_t s_demos[] = {
    {
        .key = "system_info",
        .title = "系统信息与启动流程",
        .doc_path = "docs/modules/01_system_info.md",
        .run = system_info_demo_run,
        .needs_wifi = false,
        .loops_forever = false,
    },
    {
        .key = "freertos_tasks",
        .title = "FreeRTOS 任务与队列",
        .doc_path = "docs/modules/02_freertos_tasks.md",
        .run = freertos_tasks_demo_run,
        .needs_wifi = false,
        .loops_forever = true,
    },
    {
        .key = "gpio_blink",
        .title = "GPIO 输出与 LED 闪烁",
        .doc_path = "docs/modules/03_gpio_blink.md",
        .run = gpio_blink_demo_run,
        .needs_wifi = false,
        .loops_forever = true,
    },
    {
        .key = "nvs_counter",
        .title = "NVS 键值存储",
        .doc_path = "docs/modules/04_nvs_counter.md",
        .run = nvs_counter_demo_run,
        .needs_wifi = false,
        .loops_forever = false,
    },
    {
        .key = "wifi_sta",
        .title = "Wi-Fi STA 入网",
        .doc_path = "docs/modules/05_wifi_sta.md",
        .run = wifi_sta_demo_run,
        .needs_wifi = true,
        .loops_forever = false,
    },
    {
        .key = "http_get",
        .title = "HTTP Client GET 请求",
        .doc_path = "docs/modules/06_http_get.md",
        .run = http_get_demo_run,
        .needs_wifi = true,
        .loops_forever = false,
    },
    {
        .key = "adc_oneshot",
        .title = "ADC 单次采样",
        .doc_path = "docs/modules/07_adc_oneshot.md",
        .run = adc_oneshot_demo_run,
        .needs_wifi = false,
        .loops_forever = true,
    },
    {
        .key = "uart_echo",
        .title = "UART 回显",
        .doc_path = "docs/modules/08_uart_echo.md",
        .run = uart_echo_demo_run,
        .needs_wifi = false,
        .loops_forever = true,
    },
    {
        .key = "i2c_scan",
        .title = "I2C 总线扫描",
        .doc_path = "docs/modules/09_i2c_scan.md",
        .run = i2c_scan_demo_run,
        .needs_wifi = false,
        .loops_forever = false,
    },
};

const demo_case_t *demo_registry_all(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(s_demos) / sizeof(s_demos[0]);
    }

    return s_demos;
}

const demo_case_t *demo_registry_selected(void)
{
#if CONFIG_CASE2_DEMO_SYSTEM_INFO
    return &s_demos[0];
#elif CONFIG_CASE2_DEMO_FREERTOS_TASKS
    return &s_demos[1];
#elif CONFIG_CASE2_DEMO_GPIO_BLINK
    return &s_demos[2];
#elif CONFIG_CASE2_DEMO_NVS_COUNTER
    return &s_demos[3];
#elif CONFIG_CASE2_DEMO_WIFI_STA
    return &s_demos[4];
#elif CONFIG_CASE2_DEMO_HTTP_GET
    return &s_demos[5];
#elif CONFIG_CASE2_DEMO_ADC_ONESHOT
    return &s_demos[6];
#elif CONFIG_CASE2_DEMO_UART_ECHO
    return &s_demos[7];
#elif CONFIG_CASE2_DEMO_I2C_SCAN
    return &s_demos[8];
#else
    return &s_demos[0];
#endif
}
