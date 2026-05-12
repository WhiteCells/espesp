#include "adc_reader/adc_reader.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "adc_reader";

static bool adc_calibration_init(adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t curve_config = {
        .unit_id = ADC_UNIT_1,
        .chan = CONFIG_CASE2_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&curve_config, &handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t line_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&line_config, &handle);
#endif

    if (ret == ESP_OK) {
        *out_handle = handle;
        return true;
    }

    ESP_LOGW(TAG, "ADC calibration is unavailable on this chip/efuse, raw value still works");
    *out_handle = NULL;
    return false;
}

static void adc_calibration_deinit(adc_cali_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}

esp_err_t adc_reader_run(void)
{
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    /* 单次采样模式适合低频传感器，周期由业务任务控制。 */
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, CONFIG_CASE2_ADC_CHANNEL, &channel_config));

    adc_cali_handle_t cali_handle = NULL;
    bool calibrated = adc_calibration_init(&cali_handle);

    ESP_LOGI(TAG, "read ADC1 channel %d every %d ms", CONFIG_CASE2_ADC_CHANNEL, CONFIG_CASE2_ADC_PERIOD_MS);

    while (true) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, CONFIG_CASE2_ADC_CHANNEL, &raw));

        if (calibrated) {
            int voltage_mv = 0;
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &voltage_mv));
            ESP_LOGI(TAG, "raw=%d, voltage=%d mV", raw, voltage_mv);
        } else {
            ESP_LOGI(TAG, "raw=%d", raw);
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_CASE2_ADC_PERIOD_MS));
    }

    adc_calibration_deinit(cali_handle);
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
    return ESP_OK;
}
