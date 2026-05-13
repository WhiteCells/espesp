# display

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> display: write SSD1306 OLED pattern
```

可调参数：

```text
ESPESP Menu
  -> Display module
```

然后执行：

```sh
idf.py build flash monitor
```

## 当前模块已有接口

### `esp_err_t display_run(void)`

通过 I2C 初始化 SSD1306 OLED，清屏后写入边框和简单图案。

参数：无。SDA、SCL、I2C 地址、频率和超时来自 Kconfig。

返回值：

- `ESP_OK`：初始化、清屏和图案写入成功。
- 其他 `esp_err_t`：I2C bus/device 创建或传输失败。

## 内部接口

### `static esp_err_t display_write_command(i2c_master_dev_handle_t device, uint8_t command)`

写入一条 SSD1306 命令。

参数：

- `device`：I2C 设备句柄。
- `command`：SSD1306 命令字节。

实现细节：

- 发送缓冲区是 `{0x00, command}`。
- `0x00` 是 SSD1306 控制字，表示后续字节是命令。

### `static esp_err_t display_write_data(i2c_master_dev_handle_t device, const uint8_t *data, size_t length)`

写入显存数据。

参数：

- `device`：I2C 设备句柄。
- `data`：数据缓冲区。
- `length`：数据长度。

实现细节：

- 每次最多发送 16 字节数据，加上首字节控制字 `0x40`。
- `0x40` 表示后续字节是 GDDRAM 显示数据。

### `display_init_ssd1306()`、`display_clear()`、`display_draw_pattern()`

- `display_init_ssd1306()`：发送 SSD1306 初始化命令序列。
- `display_clear()`：按 8 个 page 写 0，清空 128x64 屏幕。
- `display_draw_pattern()`：写入边框和简单图案，用于确认显示链路。

## 常用接口说明

### `i2c_master_bus_config_t`

I2C 总线配置。

本模块字段：

- `i2c_port = I2C_NUM_0`：使用 I2C0。
- `sda_io_num = CONFIG_ESPESP_DISPLAY_SDA_GPIO`：SDA GPIO。
- `scl_io_num = CONFIG_ESPESP_DISPLAY_SCL_GPIO`：SCL GPIO。
- `clk_source = I2C_CLK_SRC_DEFAULT`：默认时钟源。
- `glitch_ignore_cnt = 7`：过滤短毛刺。
- `flags.enable_internal_pullup = true`：启用内部上拉。

### `i2c_device_config_t`

I2C 设备配置。

本模块字段：

- `dev_addr_length = I2C_ADDR_BIT_LEN_7`：使用 7-bit 地址。
- `device_address = CONFIG_ESPESP_DISPLAY_I2C_ADDR`：OLED 地址，常见是 `0x3C` 或 `0x3D`。
- `scl_speed_hz = CONFIG_ESPESP_DISPLAY_I2C_HZ`：I2C 时钟频率。

### `i2c_master_bus_add_device(bus_handle, &device_config, &device)`

把一个设备挂到 I2C bus 上。

参数：

- `bus_handle`：总线句柄。
- `device_config`：设备地址和速率配置。
- `device`：输出设备句柄。

### `i2c_master_transmit(device, write_buffer, write_size, xfer_timeout_ms)`

发送 I2C 数据。

参数：

- `device`：I2C 设备句柄。
- `write_buffer`：发送缓冲区。
- `write_size`：发送字节数。
- `xfer_timeout_ms`：超时，来自 `CONFIG_ESPESP_DISPLAY_TIMEOUT_MS`。

## SSD1306 相关概念

- 128x64 单色屏通常分为 8 个 page，每个 page 高 8 像素、宽 128 列。
- 命令 `0xB0 + page` 选择 page。
- 命令 `0x00` 设置列地址低 4 位。
- 命令 `0x10` 设置列地址高 4 位。
- 数据字节的每一 bit 对应该列 page 内的一个像素。

## 可配置项

- `CONFIG_ESPESP_DISPLAY_SDA_GPIO`：SDA 引脚。
- `CONFIG_ESPESP_DISPLAY_SCL_GPIO`：SCL 引脚。
- `CONFIG_ESPESP_DISPLAY_I2C_ADDR`：OLED I2C 地址。
- `CONFIG_ESPESP_DISPLAY_I2C_HZ`：I2C 频率。
- `CONFIG_ESPESP_DISPLAY_TIMEOUT_MS`：I2C 传输超时。

## 注意事项

- 不确定 OLED 地址时，先运行 `i2c_scan`。
- 多数 OLED 模块已有上拉，裸屏或长线需要额外上拉。
- 本模块没有 framebuffer，复杂 UI 建议封装显存缓冲和字体绘制。
