# 03 led_blink: GPIO 输出与 LED 闪烁

## 学什么

- `gpio_config()` 配置 GPIO 模式。
- `gpio_set_level()` 输出高低电平。
- GPIO 编号和开发板丝印编号不是一回事。

## 怎么运行

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Module selector
  -> 03 led_blink
```

可调参数：

```text
Case2 ESP Learning
  -> LED module
```

## 看哪段代码

- `main/led/led_blink.c`

## 接口介绍

- `led_blink_run()`：初始化 LED GPIO 并周期翻转。
- 常用接口：`gpio_config()`、`gpio_set_level()`。

## 接线

- 如果使用板载 LED，只需要确认 LED GPIO。
- 如果外接 LED，建议 GPIO -> 限流电阻 -> LED -> GND。

## 注意事项

- 不同开发板 LED 引脚不同，要以原理图或板级文档为准。
- 有些板载 LED 是低电平点亮。

## 练习

- 修改闪烁周期。
- 换一个 GPIO 输出。
- 如果你的板载 LED 是低电平亮，改成日志里显示 `led_on` 而不是原始电平。
