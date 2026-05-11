# 07 adc_oneshot: ADC 单次采样

## 学什么

- `adc_oneshot_new_unit()` 初始化 ADC 单次采样单元。
- `adc_oneshot_config_channel()` 配置通道。
- `adc_oneshot_read()` 读取 raw 值。
- ADC 校准可把 raw 值转换成估算电压。
- ADC channel 不是 GPIO number。

## 怎么运行

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Demo selector
  -> 07 adc_oneshot
```

可调参数：

```text
Case2 ESP Learning
  -> ADC oneshot demo
```

## 看哪段代码

- `main/demos/adc_oneshot_demo.c`

## 接线

把可调电压、分压后的传感器输出或电位器中间脚接到 ADC 引脚。输入电压不要超过
芯片允许范围，GND 必须共地。

ESP32-S3 常见映射：

- ADC1 channel 2 通常对应 GPIO3。

## 练习

- 改变输入电压，观察 raw 和 mV。
- 修改采样周期。
- 换一个 ADC1 channel，并确认对应 GPIO。
