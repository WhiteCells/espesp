#include "pcm_stream/pcm_stream.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include "wifi_station/wifi_station.h"

static const char *TAG = "pcm_stream";

#define PCM_DEFAULT_SAMPLE_SHIFT_BITS 12
#ifndef CONFIG_ESPESP_PCM_SAMPLE_SHIFT_BITS
#define PCM_I2S_TO_PCM16_SHIFT PCM_DEFAULT_SAMPLE_SHIFT_BITS
#else
#define PCM_I2S_TO_PCM16_SHIFT CONFIG_ESPESP_PCM_SAMPLE_SHIFT_BITS
#endif

#define PCM_FRAME_SAMPLES CONFIG_ESPESP_PCM_FRAME_SAMPLES
#define PCM_MAGIC 0x314D4350U /* "PCM1" little-endian */
#define PCM_VERSION 1U
#define PCM_SAMPLE_WIDTH_BITS 16U
#define PCM_HEADER_BYTES 28U
#define PCM_MAX_PAYLOAD_BYTES (PCM_FRAME_SAMPLES * sizeof(int16_t))
#define PCM_MAX_PACKET_BYTES (PCM_HEADER_BYTES + PCM_MAX_PAYLOAD_BYTES)
#define PCM_UDP_SEND_RETRY_COUNT 8U
#define PCM_UDP_SEND_RETRY_DELAY_MS 20U
#define PCM_STATS_PERIOD_US 1000000LL
#define PCM_DROP_LOG_PERIOD_US 1000000LL

#if CONFIG_ESPESP_PCM_INPUT_SLOT_RIGHT
#define PCM_I2S_SLOT_MASK I2S_STD_SLOT_RIGHT
#define PCM_I2S_SLOT_NAME "right"
#else
#define PCM_I2S_SLOT_MASK I2S_STD_SLOT_LEFT
#define PCM_I2S_SLOT_NAME "left"
#endif

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t sequence;
    uint32_t sample_rate_hz;
    uint16_t channels;
    uint16_t sample_width_bits;
    uint32_t frame_samples;
    uint32_t payload_bytes;
} pcm_stream_packet_header_t;

typedef enum {
    PCM_STREAM_TRANSPORT_UART = 0,
    PCM_STREAM_TRANSPORT_UDP = 1,
} pcm_stream_transport_t;

typedef struct {
    pcm_stream_transport_t transport;
    i2s_chan_handle_t rx_channel;
    uart_port_t uart_port;
    int udp_socket;
    struct sockaddr_in udp_target;
} pcm_stream_context_t;

#if CONFIG_ESPESP_PCM_STREAM_TRANSPORT_UDP
static const pcm_stream_transport_t s_default_transport = PCM_STREAM_TRANSPORT_UDP;
#else
static const pcm_stream_transport_t s_default_transport = PCM_STREAM_TRANSPORT_UART;
#endif

static esp_err_t pcm_stream_create_channel(i2s_chan_handle_t *rx_channel)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, NULL, rx_channel), TAG, "create I2S RX channel");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_ESPESP_MIC_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_ESPESP_MIC_BCLK_GPIO,
            .ws = CONFIG_ESPESP_MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = CONFIG_ESPESP_MIC_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_config.slot_cfg.slot_mask = PCM_I2S_SLOT_MASK;

    esp_err_t ret = i2s_channel_init_std_mode(*rx_channel, &std_config);
    if (ret != ESP_OK) {
        i2s_del_channel(*rx_channel);
        *rx_channel = NULL;
    }

    return ret;
}

static int16_t pcm_stream_convert_sample(int32_t sample)
{
    /*
     * The I2S RX path gives us a 32-bit slot, but common MEMS microphones and
     * boards differ in how much useful headroom their samples occupy. The old
     * shift of 8 was too hot and clipped badly; 16 was too quiet on this
     * board. Keep this shift configurable so the stream can be matched to the
     * actual microphone without changing the packet format.
     */
    int32_t pcm = sample >> PCM_I2S_TO_PCM16_SHIFT;
    if (pcm > INT16_MAX) {
        pcm = INT16_MAX;
    } else if (pcm < INT16_MIN) {
        pcm = INT16_MIN;
    }
    return (int16_t)pcm;
}

static esp_err_t pcm_stream_uart_init(pcm_stream_context_t *ctx)
{
    const uart_port_t port = CONFIG_ESPESP_PCM_UART_PORT_NUM;
    uart_config_t uart_config = {
        .baud_rate = CONFIG_ESPESP_PCM_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(port, 4096, 0, 0, NULL, 0), TAG, "install UART driver");
    ctx->uart_port = port;
    ESP_RETURN_ON_ERROR(uart_param_config(port, &uart_config), TAG, "configure UART");

#if CONFIG_ESPESP_PCM_UART_USE_CUSTOM_PINS
    ESP_RETURN_ON_ERROR(uart_set_pin(port,
                                     CONFIG_ESPESP_PCM_UART_TX_GPIO,
                                     CONFIG_ESPESP_PCM_UART_RX_GPIO,
                                     UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG,
                        "configure UART pins");
#endif
    return ESP_OK;
}

static esp_err_t pcm_stream_udp_init(pcm_stream_context_t *ctx)
{
    if (strlen(CONFIG_ESPESP_PCM_UDP_HOST) == 0) {
        ESP_LOGE(TAG, "UDP target host is empty");
        return ESP_ERR_INVALID_ARG;
    }

    if (CONFIG_ESPESP_PCM_UDP_PORT <= 0) {
        ESP_LOGE(TAG, "UDP target port is invalid");
        return ESP_ERR_INVALID_ARG;
    }

    ctx->udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (ctx->udp_socket < 0) {
        ESP_LOGE(TAG, "failed to create UDP socket");
        return ESP_FAIL;
    }

    int broadcast = 1;
    if (setsockopt(ctx->udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        ESP_LOGE(TAG, "enable UDP broadcast failed: errno=%d", errno);
        close(ctx->udp_socket);
        ctx->udp_socket = -1;
        return ESP_FAIL;
    }

    ctx->udp_target.sin_family = AF_INET;
    ctx->udp_target.sin_port = htons((uint16_t)CONFIG_ESPESP_PCM_UDP_PORT);
    if (inet_pton(AF_INET, CONFIG_ESPESP_PCM_UDP_HOST, &ctx->udp_target.sin_addr) != 1) {
        ESP_LOGE(TAG, "invalid UDP host: %s", CONFIG_ESPESP_PCM_UDP_HOST);
        close(ctx->udp_socket);
        ctx->udp_socket = -1;
        return ESP_ERR_INVALID_ARG;
    }

    if (connect(ctx->udp_socket, (struct sockaddr *)&ctx->udp_target, sizeof(ctx->udp_target)) < 0) {
        ESP_LOGE(TAG,
                 "connect UDP target %s:%d failed: errno=%d",
                 CONFIG_ESPESP_PCM_UDP_HOST,
                 CONFIG_ESPESP_PCM_UDP_PORT,
                 errno);
        close(ctx->udp_socket);
        ctx->udp_socket = -1;
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void pcm_stream_log_sample_rate(void)
{
    ESP_LOGI(TAG,
             "PCM stream: sample_rate=%d Hz, channels=1, sample_width=16-bit, frame_samples=%u, shift=%d, slot=%s",
             CONFIG_ESPESP_MIC_SAMPLE_RATE_HZ,
             (unsigned int)PCM_FRAME_SAMPLES,
             PCM_I2S_TO_PCM16_SHIFT,
             PCM_I2S_SLOT_NAME);
}

static esp_err_t pcm_stream_uart_write_all(uart_port_t port, const uint8_t *data, size_t len)
{
    size_t offset = 0;
    while (offset < len) {
        int written = uart_write_bytes(port, (const char *)data + offset, len - offset);
        if (written < 0) {
            return ESP_FAIL;
        }
        if (written == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        offset += (size_t)written;
    }

    return uart_wait_tx_done(port, pdMS_TO_TICKS(1000));
}

static esp_err_t pcm_stream_send_uart(pcm_stream_context_t *ctx,
                                      const uint8_t *packet,
                                      size_t packet_bytes)
{
    return pcm_stream_uart_write_all(ctx->uart_port, packet, packet_bytes);
}

static esp_err_t pcm_stream_send_udp(pcm_stream_context_t *ctx,
                                     const uint8_t *packet,
                                     size_t packet_bytes)
{
    int last_errno = 0;
    int64_t started_us = esp_timer_get_time();

    for (uint32_t attempt = 0; attempt <= PCM_UDP_SEND_RETRY_COUNT; attempt++) {
        errno = 0;
        int ret = send(ctx->udp_socket, packet, packet_bytes, 0);
        if (ret == (int)packet_bytes) {
            return ESP_OK;
        }

        last_errno = errno;
        if (ret >= 0) {
            ESP_LOGE(TAG,
                     "short UDP send: ret=%d expected=%u target=%s:%d",
                     ret,
                     (unsigned int)packet_bytes,
                     CONFIG_ESPESP_PCM_UDP_HOST,
                     CONFIG_ESPESP_PCM_UDP_PORT);
            return ESP_FAIL;
        }

        if (last_errno != ENOMEM && last_errno != EAGAIN && last_errno != ENOBUFS) {
            ESP_LOGE(TAG,
                     "UDP send failed: ret=%d expected=%u errno=%d target=%s:%d free_heap=%" PRIu32,
                     ret,
                     (unsigned int)packet_bytes,
                     last_errno,
                     CONFIG_ESPESP_PCM_UDP_HOST,
                     CONFIG_ESPESP_PCM_UDP_PORT,
                     esp_get_free_heap_size());
            return ESP_FAIL;
        }

        vTaskDelay(pdMS_TO_TICKS(PCM_UDP_SEND_RETRY_DELAY_MS));
    }

    ESP_LOGW(TAG,
             "drop UDP frame after retries: expected=%u errno=%d target=%s:%d free_heap=%" PRIu32
             " waited_ms=%" PRId64,
             (unsigned int)packet_bytes,
             last_errno,
             CONFIG_ESPESP_PCM_UDP_HOST,
             CONFIG_ESPESP_PCM_UDP_PORT,
             esp_get_free_heap_size(),
             (esp_timer_get_time() - started_us) / 1000);
    return ESP_ERR_NO_MEM;
}

static esp_err_t pcm_stream_send_frame(pcm_stream_context_t *ctx,
                                       uint32_t sequence,
                                       const int16_t *pcm,
                                       size_t sample_count)
{
    if (sample_count > PCM_FRAME_SAMPLES) {
        return ESP_ERR_INVALID_SIZE;
    }

    pcm_stream_packet_header_t header = {
        .magic = PCM_MAGIC,
        .version = PCM_VERSION,
        .header_size = (uint16_t)PCM_HEADER_BYTES,
        .sequence = sequence,
        .sample_rate_hz = CONFIG_ESPESP_MIC_SAMPLE_RATE_HZ,
        .channels = 1,
        .sample_width_bits = PCM_SAMPLE_WIDTH_BITS,
        .frame_samples = (uint32_t)sample_count,
        .payload_bytes = (uint32_t)(sample_count * sizeof(int16_t)),
    };

    uint8_t packet[PCM_MAX_PACKET_BYTES];
    memcpy(packet, &header, sizeof(header));
    memcpy(packet + sizeof(header), pcm, header.payload_bytes);

    if (ctx->transport == PCM_STREAM_TRANSPORT_UART) {
        return pcm_stream_send_uart(ctx, packet, sizeof(header) + header.payload_bytes);
    }

    return pcm_stream_send_udp(ctx, packet, sizeof(header) + header.payload_bytes);
}

esp_err_t pcm_stream_run(void)
{
    pcm_stream_context_t ctx = {
        .transport = s_default_transport,
        .rx_channel = NULL,
        .uart_port = UART_NUM_MAX,
        .udp_socket = -1,
        .udp_target = { 0 },
    };
    bool i2s_enabled = false;

    esp_err_t ret = pcm_stream_create_channel(&ctx.rx_channel);
    if (ret != ESP_OK) {
        return ret;
    }

    if (ctx.transport == PCM_STREAM_TRANSPORT_UART) {
        ret = pcm_stream_uart_init(&ctx);
    } else {
        ret = wifi_station_connect();
        if (ret == ESP_OK) {
            esp_err_t ps_ret = esp_wifi_set_ps(WIFI_PS_NONE);
            if (ps_ret != ESP_OK) {
                ESP_LOGW(TAG, "disable Wi-Fi power save failed: %s", esp_err_to_name(ps_ret));
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            ret = pcm_stream_udp_init(&ctx);
        }
    }

    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = i2s_channel_enable(ctx.rx_channel);
    if (ret != ESP_OK) {
        goto cleanup;
    }
    i2s_enabled = true;

    pcm_stream_log_sample_rate();
    if (ctx.transport == PCM_STREAM_TRANSPORT_UART) {
        ESP_LOGI(TAG,
                 "transport=uart, port=%d, baud=%d",
                 CONFIG_ESPESP_PCM_UART_PORT_NUM,
                 CONFIG_ESPESP_PCM_UART_BAUD_RATE);
    } else {
        ESP_LOGI(TAG,
                 "transport=udp, target=%s:%d",
                 CONFIG_ESPESP_PCM_UDP_HOST,
                 CONFIG_ESPESP_PCM_UDP_PORT);
    }

    int32_t raw_samples[PCM_FRAME_SAMPLES];
    int16_t pcm_samples[PCM_FRAME_SAMPLES];
    uint32_t sequence = 0;
    uint32_t frames_sent = 0;
    uint32_t frames_dropped = 0;
    int64_t last_stats_us = esp_timer_get_time();
    int64_t last_drop_log_us = 0;

    while (true) {
        size_t bytes_read = 0;
        ret = i2s_channel_read(ctx.rx_channel,
                               raw_samples,
                               sizeof(raw_samples),
                               &bytes_read,
                               pdMS_TO_TICKS(1000));
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2S read timeout");
            continue;
        }
        if (ret != ESP_OK) {
            goto cleanup;
        }

        size_t sample_count = bytes_read / sizeof(raw_samples[0]);
        for (size_t i = 0; i < sample_count; i++) {
            pcm_samples[i] = pcm_stream_convert_sample(raw_samples[i]);
        }

        ret = pcm_stream_send_frame(&ctx, sequence++, pcm_samples, sample_count);
        if (ret != ESP_OK) {
            if (ctx.transport == PCM_STREAM_TRANSPORT_UDP && ret == ESP_ERR_NO_MEM) {
                frames_dropped++;
                int64_t now_us = esp_timer_get_time();
                if (now_us - last_drop_log_us >= PCM_DROP_LOG_PERIOD_US) {
                    ESP_LOGW(TAG,
                             "UDP frames are being dropped: sent=%" PRIu32 ", dropped=%" PRIu32
                             ", target=%s:%d, free_heap=%" PRIu32,
                             frames_sent,
                             frames_dropped,
                             CONFIG_ESPESP_PCM_UDP_HOST,
                             CONFIG_ESPESP_PCM_UDP_PORT,
                             esp_get_free_heap_size());
                    last_drop_log_us = now_us;
                }
            } else {
                ESP_LOGE(TAG, "send frame failed: %s", esp_err_to_name(ret));
                break;
            }
        } else {
            frames_sent++;
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us - last_stats_us >= PCM_STATS_PERIOD_US) {
            if (ctx.transport == PCM_STREAM_TRANSPORT_UDP) {
                ESP_LOGI(TAG,
                         "udp stats: sent=%" PRIu32 ", dropped=%" PRIu32 ", free_heap=%" PRIu32,
                         frames_sent,
                         frames_dropped,
                         esp_get_free_heap_size());
            }
            last_stats_us = now_us;
        }
    }

cleanup:
    if (ctx.transport == PCM_STREAM_TRANSPORT_UDP && ctx.udp_socket >= 0) {
        close(ctx.udp_socket);
    }

    if (ctx.transport == PCM_STREAM_TRANSPORT_UART && ctx.uart_port != UART_NUM_MAX) {
        uart_driver_delete(ctx.uart_port);
    }

    if (i2s_enabled) {
        i2s_channel_disable(ctx.rx_channel);
    }
    i2s_del_channel(ctx.rx_channel);
    return ret;
}
