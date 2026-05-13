#include "i2c_scan/i2c_scan.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "i2c_scan";

esp_err_t i2c_scan_run(void)
{
    /* 新版 I2C master bus/device API 比旧 i2c_cmd_link 更适合新项目。 */
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_ESPESP_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_ESPESP_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    ESP_LOGI(TAG, "scan I2C bus: SDA=GPIO%d, SCL=GPIO%d, timeout=%d ms",
             CONFIG_ESPESP_I2C_SDA_GPIO,
             CONFIG_ESPESP_I2C_SCL_GPIO,
             CONFIG_ESPESP_I2C_PROBE_TIMEOUT_MS);

    int found = 0;
    for (uint8_t address = 0x03; address < 0x78; address++) {
        esp_err_t ret = i2c_master_probe(bus_handle, address, CONFIG_ESPESP_I2C_PROBE_TIMEOUT_MS);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "found device at 0x%02x", address);
            found++;
        }
    }

    if (found == 0) {
        ESP_LOGW(TAG, "no I2C device found. Check wiring, pull-ups, SDA/SCL pins, and device power.");
    } else {
        ESP_LOGI(TAG, "scan complete, found %d device(s)", found);
    }

    ESP_ERROR_CHECK(i2c_del_master_bus(bus_handle));
    return ESP_OK;
}
