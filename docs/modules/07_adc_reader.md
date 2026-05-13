# 07 adc_reader: ADC 单次采样

## 模块概览

- `adc_oneshot_new_unit()` 初始化 ADC 单次采样单元。
- `adc_oneshot_config_channel()` 配置通道。
- `adc_oneshot_read()` 读取 raw 值。
- ADC 校准可把 raw 值转换成估算电压。
- ADC channel 不是 GPIO number。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 07 adc_reader
```

可调参数：

```text
ESPESP Menu
  -> ADC reader module
```

## 源码位置

- `main/adc_reader/adc_reader.c`

## 当前模块接口参考

- `adc_reader_run()`：周期读取 ADC raw 和可用时的校准电压。
- `adc_calibration_init()`：尝试创建 ADC 校准句柄，失败时仍保留 raw 采样。

## 常用接口说明

- `adc_oneshot_unit_init_cfg_t`：配置 ADC 单次采样单元，本模块使用 ADC1。
- `adc_oneshot_new_unit()`：创建 ADC oneshot 单元。
- `adc_oneshot_chan_cfg_t`：配置通道衰减和位宽。
- `adc_oneshot_config_channel()`：把通道配置应用到指定 ADC channel。
- `adc_oneshot_read()`：读取一次 raw 值。
- `adc_cali_create_scheme_curve_fitting()`：创建曲线拟合校准方案。
- `adc_cali_raw_to_voltage()`：把 raw 转换成估算毫伏值。
- `adc_cali_delete_scheme_curve_fitting()`：释放校准句柄。

## 配置项

- `CONFIG_ESPESP_ADC_CHANNEL`：ADC1 channel 编号，不是 GPIO 编号。
- `CONFIG_ESPESP_ADC_PERIOD_MS`：采样周期，单位 ms。

## 接线

把可调电压、分压后的传感器输出或电位器中间脚接到 ADC 引脚。输入电压不要超过
芯片允许范围，GND 必须共地。

ESP32-S3 常见映射：

- ADC1 channel 2 通常对应 GPIO3。

## 注意事项

- ADC 不能直接测超过芯片限制的电压。
- 校准不可用时 raw 值仍可用于观察趋势。

## 扩展方向

- 改变输入电压，观察 raw 和 mV。
- 修改采样周期。
- 换一个 ADC1 channel，并确认对应 GPIO。
