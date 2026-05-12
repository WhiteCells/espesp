#include "display/display.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "display";

static esp_err_t display_write_command(i2c_master_dev_handle_t device, uint8_t command)
{
    uint8_t payload[] = { 0x00, command };
    return i2c_master_transmit(device, payload, sizeof(payload), CONFIG_CASE2_DISPLAY_TIMEOUT_MS);
}

static esp_err_t display_write_data(i2c_master_dev_handle_t device, const uint8_t *data, size_t length)
{
    uint8_t payload[17] = { 0x40 };

    while (length > 0) {
        size_t chunk = length > 16 ? 16 : length;
        memcpy(&payload[1], data, chunk);
        ESP_RETURN_ON_ERROR(i2c_master_transmit(device,
                                                payload,
                                                chunk + 1,
                                                CONFIG_CASE2_DISPLAY_TIMEOUT_MS),
                            TAG,
                            "write OLED data");
        data += chunk;
        length -= chunk;
    }

    return ESP_OK;
}

static esp_err_t display_init_ssd1306(i2c_master_dev_handle_t device)
{
    static const uint8_t init_commands[] = {
        0xAE, 0x20, 0x00, 0x40, 0xA1, 0xC8, 0x81, 0x7F,
        0xA4, 0xA6, 0xA8, 0x3F, 0xD3, 0x00, 0xD5, 0x80,
        0xD9, 0xF1, 0xDA, 0x12, 0xDB, 0x40, 0x8D, 0x14,
        0xAF,
    };

    /* SSD1306 的命令通道控制字是 0x00，数据通道控制字是 0x40。 */
    for (size_t i = 0; i < sizeof(init_commands); i++) {
        ESP_RETURN_ON_ERROR(display_write_command(device, init_commands[i]), TAG, "init OLED");
    }

    return ESP_OK;
}

static esp_err_t display_clear(i2c_master_dev_handle_t device)
{
    uint8_t empty[128] = { 0 };

    for (uint8_t page = 0; page < 8; page++) {
        ESP_RETURN_ON_ERROR(display_write_command(device, 0xB0 + page), TAG, "set OLED page");
        ESP_RETURN_ON_ERROR(display_write_command(device, 0x00), TAG, "set OLED low column");
        ESP_RETURN_ON_ERROR(display_write_command(device, 0x10), TAG, "set OLED high column");
        ESP_RETURN_ON_ERROR(display_write_data(device, empty, sizeof(empty)), TAG, "clear OLED page");
    }

    return ESP_OK;
}

static esp_err_t display_draw_pattern(i2c_master_dev_handle_t device)
{
    uint8_t line[128];

    for (uint8_t page = 0; page < 8; page++) {
        for (size_t column = 0; column < sizeof(line); column++) {
            if (page == 0 || page == 7 || column == 0 || column == 127) {
                line[column] = 0xFF;
            } else if (((column / 8) + page) % 2 == 0) {
                line[column] = 0x18;
            } else {
                line[column] = 0x00;
            }
        }

        ESP_RETURN_ON_ERROR(display_write_command(device, 0xB0 + page), TAG, "set OLED page");
        ESP_RETURN_ON_ERROR(display_write_command(device, 0x00), TAG, "set OLED low column");
        ESP_RETURN_ON_ERROR(display_write_command(device, 0x10), TAG, "set OLED high column");
        ESP_RETURN_ON_ERROR(display_write_data(device, line, sizeof(line)), TAG, "draw OLED page");
    }

    return ESP_OK;
}

esp_err_t display_run(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_CASE2_DISPLAY_SDA_GPIO,
        .scl_io_num = CONFIG_CASE2_DISPLAY_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus_handle = NULL;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &bus_handle), TAG, "create display I2C bus");

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_CASE2_DISPLAY_I2C_ADDR,
        .scl_speed_hz = CONFIG_CASE2_DISPLAY_I2C_HZ,
    };

    i2c_master_dev_handle_t device = NULL;
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &device_config, &device);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG,
                 "SSD1306 display: SDA=GPIO%d, SCL=GPIO%d, addr=0x%02x",
                 CONFIG_CASE2_DISPLAY_SDA_GPIO,
                 CONFIG_CASE2_DISPLAY_SCL_GPIO,
                 CONFIG_CASE2_DISPLAY_I2C_ADDR);
        ret = display_init_ssd1306(device);
    }
    if (ret == ESP_OK) {
        ret = display_clear(device);
    }
    if (ret == ESP_OK) {
        ret = display_draw_pattern(device);
    }

    if (device != NULL) {
        ESP_ERROR_CHECK(i2c_master_bus_rm_device(device));
    }
    ESP_ERROR_CHECK(i2c_del_master_bus(bus_handle));

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "display pattern was written");
    }

    return ret;
}
