#include "registry/app_registry.h"

#include "sdkconfig.h"

#include "adc_reader/adc_reader.h"
#include "display/display.h"
#include "http_client/http_client.h"
#include "https_client/https_client.h"
#include "http_server/http_server.h"
#include "https_server/https_server.h"
#include "i2c_scan/i2c_scan.h"
#include "led/led_blink.h"
#include "microphone/microphone.h"
#include "mqtt_client/mqtt_client_app.h"
#include "nvs_counter/nvs_counter.h"
#include "pcm_stream/pcm_stream.h"
#include "micro_wake_word/micro_wake_word.h"
#include "rtos_tasks/rtos_tasks.h"
#include "speaker/speaker.h"
#include "speaker_client/speaker_client.h"
#include "system_info/system_info.h"
#include "vad/vad.h"
#include "voice_callback/voice_callback.h"
#include "voice_client/voice_client.h"
#include "wake_word/wake_word.h"
#include "websocket_client/websocket_client.h"
#include "websocket_server/websocket_server.h"
#include "uart_echo/uart_echo.h"
#include "wifi_station/wifi_station.h"

#if CONFIG_ESPESP_PCM_STREAM_TRANSPORT_UDP
#define PCM_STREAM_NEEDS_WIFI true
#else
#define PCM_STREAM_NEEDS_WIFI false
#endif

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
        .key = "http_client",
        .title = "HTTP Client GET 请求",
        .doc_path = "docs/modules/06_http_client.md",
        .run = http_client_run,
        .needs_wifi = true,
        .runs_forever = false,
    },
    {
        .key = "https_client",
        .title = "HTTPS Client 安全 GET 请求",
        .doc_path = "docs/modules/20_https_client.md",
        .run = https_client_run,
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
        .key = "pcm_stream",
        .title = "PCM 音频流到电脑",
        .doc_path = "docs/modules/16_pcm_stream.md",
        .run = pcm_stream_run,
        .needs_wifi = PCM_STREAM_NEEDS_WIFI,
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
        .key = "speaker_client",
        .title = "WebSocket 音频播放客户端",
        .doc_path = "docs/modules/17_speaker_client.md",
        .run = speaker_client_run,
        .needs_wifi = true,
        .runs_forever = true,
    },
    {
        .key = "voice_client",
        .title = "实时语音对话客户端",
        .doc_path = "docs/modules/18_voice_client.md",
        .run = voice_client_run,
        .needs_wifi = true,
        .runs_forever = true,
    },
    {
        .key = "wake_word",
        .title = "本地 WakeNet 唤醒词检测",
        .doc_path = "docs/modules/21_wake_word.md",
        .run = wake_word_run,
        .needs_wifi = false,
        .runs_forever = true,
    },
    {
        .key = "micro_wake_word",
        .title = "本地 microWakeWord 唤醒词检测",
        .doc_path = "docs/modules/22_micro_wake_word.md",
        .run = micro_wake_word_run,
        .needs_wifi = false,
        .runs_forever = true,
    },
    {
        .key = "vad",
        .title = "本地语音活动检测",
        .doc_path = "docs/modules/23_vad.md",
        .run = vad_run,
        .needs_wifi = false,
        .runs_forever = true,
    },
    {
        .key = "voice_callback",
        .title = "全双工本地语音回放",
        .doc_path = "docs/modules/19_voice_callback.md",
        .run = voice_callback_run,
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
        .run = mqtt_client_run,
        .needs_wifi = true,
        .runs_forever = true,
    },
    {
        .key = "http_server",
        .title = "HTTP 局域网服务",
        .doc_path = "docs/modules/14_http_server.md",
        .run = http_server_run,
        .needs_wifi = true,
        .runs_forever = true,
    },
    {
        .key = "https_server",
        .title = "HTTPS 安全局域网服务",
        .doc_path = "docs/modules/15_https_server.md",
        .run = https_server_run,
        .needs_wifi = true,
        .runs_forever = true,
    },
    {
        .key = "websocket_server",
        .title = "WebSocket 服务端实时通信",
        .doc_path = "docs/modules/26_websocket_server.md",
        .run = websocket_server_run,
        .needs_wifi = true,
        .runs_forever = true,
    },
    {
        .key = "websocket_client",
        .title = "WebSocket 客户端实时通信",
        .doc_path = "docs/modules/26_websocket_client.md",
        .run = websocket_client_run,
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
#if CONFIG_ESPESP_MODULE_SYSTEM_INFO
    return &s_cases[0];
#elif CONFIG_ESPESP_MODULE_RTOS_TASKS
    return &s_cases[1];
#elif CONFIG_ESPESP_MODULE_LED_BLINK
    return &s_cases[2];
#elif CONFIG_ESPESP_MODULE_NVS_COUNTER
    return &s_cases[3];
#elif CONFIG_ESPESP_MODULE_WIFI_STATION
    return &s_cases[4];
#elif CONFIG_ESPESP_MODULE_HTTP_CLIENT
    return &s_cases[5];
#elif CONFIG_ESPESP_MODULE_HTTPS_CLIENT
    return &s_cases[6];
#elif CONFIG_ESPESP_MODULE_ADC_READER
    return &s_cases[7];
#elif CONFIG_ESPESP_MODULE_UART_ECHO
    return &s_cases[8];
#elif CONFIG_ESPESP_MODULE_I2C_SCAN
    return &s_cases[9];
#elif CONFIG_ESPESP_MODULE_MICROPHONE
    return &s_cases[10];
#elif CONFIG_ESPESP_MODULE_PCM_STREAM
    return &s_cases[11];
#elif CONFIG_ESPESP_MODULE_SPEAKER
    return &s_cases[12];
#elif CONFIG_ESPESP_MODULE_SPEAKER_CLIENT
    return &s_cases[13];
#elif CONFIG_ESPESP_MODULE_VOICE_CLIENT
    return &s_cases[14];
#elif CONFIG_ESPESP_MODULE_WAKE_WORD
    return &s_cases[15];
#elif CONFIG_ESPESP_MODULE_MICRO_WAKE_WORD
    return &s_cases[16];
#elif CONFIG_ESPESP_MODULE_VAD
    return &s_cases[17];
#elif CONFIG_ESPESP_MODULE_VOICE_CALLBACK
    return &s_cases[18];
#elif CONFIG_ESPESP_MODULE_DISPLAY
    return &s_cases[19];
#elif CONFIG_ESPESP_MODULE_MQTT_CLIENT
    return &s_cases[20];
#elif CONFIG_ESPESP_MODULE_HTTP_SERVER
    return &s_cases[21];
#elif CONFIG_ESPESP_MODULE_HTTPS_SERVER
    return &s_cases[22];
#elif CONFIG_ESPESP_MODULE_WEBSOCKET_SERVER
    return &s_cases[23];
#elif CONFIG_ESPESP_MODULE_WEBSOCKET_CLIENT
    return &s_cases[24];
#else
    return &s_cases[0];
#endif
}
