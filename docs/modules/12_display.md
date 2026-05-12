# 12 display: I2C OLED 显示屏

## 学什么

- `i2c_new_master_bus()` 创建 I2C 总线。
- `i2c_master_bus_add_device()` 绑定 SSD1306 地址。
- `i2c_master_transmit()` 写命令和显存数据。
- SSD1306 常用控制字：命令 `0x00`，数据 `0x40`。

## 怎么运行

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Module selector
  -> 12 display
```

可调参数：

```text
Case2 ESP Learning
  -> Display module
```

## 看哪段代码

- `main/display/display.c`

## 接口介绍

- `display_run()`：初始化 SSD1306 OLED，清屏并写入简单图案。
- 常用接口：`i2c_master_bus_add_device()`、`i2c_master_transmit()`。

## 接线

默认配置：

- SDA GPIO8 -> OLED SDA
- SCL GPIO9 -> OLED SCL
- 3V3 -> OLED VCC
- GND -> OLED GND

## 注意事项

- OLED 常见地址是 `0x3C` 或 `0x3D`，不确定时先运行 `i2c_scan`。
- 多数 OLED 模块自带上拉，裸屏或长线需要外部上拉。
- 本模块直接写 SSD1306 命令，复杂界面建议再封装 framebuffer。
