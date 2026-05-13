# led

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> led_blink: LED GPIO output
```

可调参数：

```text
ESPESP Menu
  -> LED module
```

然后执行：

```sh
idf.py build flash monitor
```

## 当前模块已有接口

### `esp_err_t led_blink_run(void)`

配置 LED GPIO 为输出，并按固定周期翻转电平。

参数：无。

返回值：

- 正常情况下不会返回。
- GPIO 配置或写电平失败时会被 `ESP_ERROR_CHECK()` 捕获并触发错误处理。

## 常用接口说明

### `esp_err_t gpio_config(const gpio_config_t *pGPIOConfig)`

批量配置 GPIO。

参数：

- `pGPIOConfig`：GPIO 配置结构体指针。

相关结构体 `gpio_config_t`：

- `pin_bit_mask`：要配置的 GPIO 位掩码。本模块使用 `1ULL << CONFIG_ESPESP_LED_GPIO`。
- `mode`：GPIO 模式。本模块使用 `GPIO_MODE_OUTPUT`。
- `pull_up_en`：是否启用内部上拉。本模块禁用。
- `pull_down_en`：是否启用内部下拉。本模块禁用。
- `intr_type`：中断类型。本模块使用 `GPIO_INTR_DISABLE`。

### `esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)`

设置输出电平。

参数：

- `gpio_num`：GPIO 编号，本模块来自 `CONFIG_ESPESP_LED_GPIO`。
- `level`：输出电平，`0` 为低电平，非 0 为高电平。

返回值：

- `ESP_OK`：设置成功。
- 其他错误：GPIO 编号无效或当前 GPIO 不支持输出。

## 可配置项

- `CONFIG_ESPESP_LED_GPIO`：LED 所在 GPIO 编号。
- `CONFIG_ESPESP_LED_PERIOD_MS`：翻转周期，单位 ms。

## 注意事项

- GPIO 编号不是开发板丝印编号，需要查原理图。
- 部分板载 LED 是低电平点亮，看到 `level=0` 时亮是正常现象。
- 外接 LED 要串联限流电阻。
