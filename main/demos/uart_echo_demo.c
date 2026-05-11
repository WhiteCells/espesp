#include "demos/uart_echo_demo.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "uart_echo";
static const int RX_BUF_SIZE = 256;

esp_err_t uart_echo_demo_run(void)
{
    const uart_port_t port = CONFIG_CASE2_UART_PORT_NUM;
    uart_config_t uart_config = {
        .baud_rate = CONFIG_CASE2_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(port, RX_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(port, &uart_config));

#if CONFIG_CASE2_UART_USE_CUSTOM_PINS
    ESP_ERROR_CHECK(uart_set_pin(port,
                                 CONFIG_CASE2_UART_TX_GPIO,
                                 CONFIG_CASE2_UART_RX_GPIO,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
#endif

    const char *hello = "UART echo demo ready. Type text and it will be sent back.\r\n";
    ESP_ERROR_CHECK(uart_write_bytes(port, hello, strlen(hello)));
    ESP_LOGI(TAG, "UART%d echo at %d baud", port, CONFIG_CASE2_UART_BAUD_RATE);

    uint8_t data[RX_BUF_SIZE];
    while (true) {
        int len = uart_read_bytes(port, data, sizeof(data), pdMS_TO_TICKS(1000));
        if (len > 0) {
            ESP_LOGI(TAG, "received %d bytes", len);
            ESP_ERROR_CHECK(uart_write_bytes(port, (const char *)data, len));
        }
    }

    return ESP_OK;
}
