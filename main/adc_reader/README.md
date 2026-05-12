# adc_reader

## 模块接口

### `esp_err_t adc_reader_run(void)`

配置 ADC1 单次采样通道，周期读取 raw 值，并在支持校准时转换为 mV。

参数：无。ADC channel 和采样周期来自 Kconfig。

返回值：

- 正常情况下不会返回。
- ADC 初始化、配置或读取失败时会被 `ESP_ERROR_CHECK()` 捕获。

## 使用的 ESP-IDF 接口和结构体

### `adc_oneshot_unit_init_cfg_t`

ADC 单元初始化配置。

本模块使用字段：

- `unit_id`：ADC 单元，本模块固定为 `ADC_UNIT_1`。

### `esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *init_config, adc_oneshot_unit_handle_t *ret_unit)`

创建 ADC oneshot 单元。

参数：

- `init_config`：ADC 单元配置。
- `ret_unit`：输出 ADC 单元句柄。

返回值：

- `ESP_OK`：创建成功。
- 其他错误：参数错误、内存不足或硬件资源占用。

### `adc_oneshot_chan_cfg_t`

ADC 通道配置。

本模块使用字段：

- `atten`：衰减。本模块使用 `ADC_ATTEN_DB_12`，可测范围更大。
- `bitwidth`：采样位宽。本模块使用 `ADC_BITWIDTH_DEFAULT`。

### `adc_oneshot_config_channel(adc_handle, channel, &channel_config)`

配置某个 ADC 通道。

参数：

- `adc_handle`：ADC 单元句柄。
- `channel`：ADC channel，本模块来自 `CONFIG_CASE2_ADC_CHANNEL`。
- `channel_config`：通道配置。

注意：ADC channel 不是 GPIO number，需要查芯片映射。

### `adc_oneshot_read(adc_handle, channel, &raw)`

读取一次 ADC raw 值。

参数：

- `adc_handle`：ADC 单元句柄。
- `channel`：ADC channel。
- `raw`：输出原始采样值。

### ADC 校准接口

本模块根据芯片支持情况使用：

- `adc_cali_create_scheme_curve_fitting()`：曲线拟合校准。
- `adc_cali_create_scheme_line_fitting()`：线性拟合校准。
- `adc_cali_raw_to_voltage()`：把 raw 转成估算 mV。

校准配置字段：

- `unit_id`：ADC 单元。
- `chan`：ADC 通道，曲线拟合需要。
- `atten`：必须和通道配置一致。
- `bitwidth`：必须和通道配置一致。

## 可配置项

- `CONFIG_CASE2_ADC_CHANNEL`：ADC1 channel。
- `CONFIG_CASE2_ADC_PERIOD_MS`：读取周期，单位 ms。

## 注意事项

- 输入电压不能超过芯片 ADC 允许范围。
- 分压电路和 ESP 必须共地。
- 校准不可用时 raw 值仍可用于观察趋势，但不能直接当作精确电压。
