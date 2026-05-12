# i2c_scan

## 模块接口

### `esp_err_t i2c_scan_run(void)`

创建 I2C master bus，扫描 `0x03` 到 `0x77` 的 7-bit 地址，打印有 ACK 的设备。

参数：无。SDA、SCL 和 probe 超时来自 Kconfig。

返回值：

- `ESP_OK`：扫描流程完成。
- I2C bus 创建或删除失败时会被 `ESP_ERROR_CHECK()` 捕获。

## 使用的 ESP-IDF 接口和结构体

### `i2c_master_bus_config_t`

I2C master bus 配置。

本模块使用字段：

- `i2c_port`：I2C 控制器端口，本模块使用 `I2C_NUM_0`。
- `sda_io_num`：SDA GPIO，来自 `CONFIG_CASE2_I2C_SDA_GPIO`。
- `scl_io_num`：SCL GPIO，来自 `CONFIG_CASE2_I2C_SCL_GPIO`。
- `clk_source`：I2C 时钟源，本模块使用 `I2C_CLK_SRC_DEFAULT`。
- `glitch_ignore_cnt`：毛刺过滤计数，本模块使用 `7`。
- `flags.enable_internal_pullup`：是否启用内部上拉，本模块启用。

### `i2c_new_master_bus(const i2c_master_bus_config_t *bus_config, i2c_master_bus_handle_t *ret_bus_handle)`

创建 I2C master bus。

参数：

- `bus_config`：总线配置。
- `ret_bus_handle`：输出 bus 句柄。

返回值：

- `ESP_OK`：创建成功。
- 其他错误：GPIO 不合法、端口占用或内存不足。

### `i2c_master_probe(i2c_master_bus_handle_t bus_handle, uint16_t address, int xfer_timeout_ms)`

探测某个地址是否 ACK。

参数：

- `bus_handle`：I2C bus 句柄。
- `address`：7-bit I2C 地址，不包含读写位。
- `xfer_timeout_ms`：本次探测超时，来自 `CONFIG_CASE2_I2C_PROBE_TIMEOUT_MS`。

返回值：

- `ESP_OK`：该地址有设备 ACK。
- `ESP_ERR_NOT_FOUND` 或超时类错误：没有响应或通信异常。

## 可配置项

- `CONFIG_CASE2_I2C_SDA_GPIO`：SDA 引脚。
- `CONFIG_CASE2_I2C_SCL_GPIO`：SCL 引脚。
- `CONFIG_CASE2_I2C_PROBE_TIMEOUT_MS`：探测超时。

## 注意事项

- I2C 需要上拉，内部上拉只适合短线低速学习。
- 扫描到设备只能证明地址 ACK，不代表设备寄存器协议正确。
- OLED 常见地址 `0x3C` 是 7-bit 地址，不要左移。
