# 03 led_blink: GPIO 输出与 LED 闪烁

## 模块概览

- `gpio_config()` 配置 GPIO 模式。
- `gpio_set_level()` 输出高低电平。
- GPIO 编号和开发板丝印编号不是一回事。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 03 led_blink
```

可调参数：

```text
ESPESP Menu
  -> LED module
```

## 源码位置

- `main/led/led_blink.c`

## 当前模块接口参考

- `led_blink_run()`：初始化 LED GPIO 并周期翻转。

## 常用接口说明

- `gpio_config()`：一次性配置 GPIO 模式、上下拉、中断类型和 pin mask。
- `gpio_set_level()`：设置输出 GPIO 的高低电平。
- `vTaskDelay()`：控制闪烁周期，同时让出 CPU。
- `pdMS_TO_TICKS()`：把毫秒转换成 FreeRTOS tick。

## 配置项

- `CONFIG_ESPESP_LED_GPIO`：LED 使用的 GPIO 编号，必须按开发板原理图确认。
- `CONFIG_ESPESP_LED_PERIOD_MS`：LED 翻转周期，单位 ms。

## 接线

- 如果使用板载 LED，只需要确认 LED GPIO。
- 如果外接 LED，建议 GPIO -> 限流电阻 -> LED -> GND。

## 注意事项

- 不同开发板 LED 引脚不同，要以原理图或板级文档为准。
- 有些板载 LED 是低电平点亮。

## 扩展方向

- 修改闪烁周期。
- 换一个 GPIO 输出。
- 如果你的板载 LED 是低电平亮，改成日志里显示 `led_on` 而不是原始电平。
