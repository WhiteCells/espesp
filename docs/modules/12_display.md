# 12 display: I2C OLED 显示屏

## 模块概览

- `i2c_new_master_bus()` 创建 I2C 总线。
- `i2c_master_bus_add_device()` 绑定 SSD1306 地址。
- `i2c_master_transmit()` 写命令和显存数据。
- SSD1306 常用控制字：命令 `0x00`，数据 `0x40`。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 12 display
```

可调参数：

```text
ESPESP Menu
  -> Display module
```

## 源码位置

- `main/display/display.c`

## 当前模块接口参考

- `display_run()`：初始化 SSD1306 OLED，清屏并写入简单图案。
- `display_write_command()`：向 SSD1306 写单字节命令。
- `display_write_data()`：按 16 字节分片写显存数据。
- `display_init_ssd1306()`：发送 SSD1306 初始化命令序列。
- `display_clear()`：清空所有 page。
- `display_draw_pattern()`：写入简单测试图案。

## 常用接口说明

- `i2c_master_bus_config_t`：配置 I2C master bus 的 SDA/SCL 引脚和时钟源。
- `i2c_new_master_bus()`：创建 I2C bus。
- `i2c_device_config_t`：配置设备地址和总线频率。
- `i2c_master_bus_add_device()`：把 OLED 加入 I2C bus，得到 device handle。
- `i2c_master_transmit()`：发送命令或数据 buffer。
- `i2c_master_bus_rm_device()`、`i2c_del_master_bus()`：释放设备和总线资源。

## 配置项

- `CONFIG_ESPESP_DISPLAY_SDA_GPIO`：OLED I2C SDA GPIO。
- `CONFIG_ESPESP_DISPLAY_SCL_GPIO`：OLED I2C SCL GPIO。
- `CONFIG_ESPESP_DISPLAY_I2C_ADDR`：OLED 7-bit I2C 地址，常见为 `0x3C` 或 `0x3D`。
- `CONFIG_ESPESP_DISPLAY_I2C_HZ`：I2C 总线频率。
- `CONFIG_ESPESP_DISPLAY_TIMEOUT_MS`：I2C 传输超时，单位 ms。

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
