# 09 i2c_scan: I2C 总线扫描

## 模块概览

- `i2c_new_master_bus()` 创建 I2C master bus。
- `i2c_master_probe()` 检查地址是否 ACK。
- I2C 地址通常是 7-bit 地址。
- SDA/SCL 需要上拉。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 09 i2c_scan
```

可调参数：

```text
ESPESP Menu
  -> I2C scan module
```

## 源码位置

- `main/i2c_scan/i2c_scan.c`

## 当前模块接口参考

- `i2c_scan_run()`：扫描总线上的 7-bit 地址。

## 常用接口说明

- `i2c_master_bus_config_t`：配置 I2C 端口、SDA/SCL 引脚、时钟源和内部上拉。
- `i2c_new_master_bus()`：创建 I2C master bus。
- `i2c_master_probe()`：向指定 7-bit 地址发送探测，判断是否 ACK。
- `i2c_del_master_bus()`：删除 I2C master bus，释放驱动资源。
- `pdMS_TO_TICKS()`：把扫描间隔或超时换算成 tick 时常会用到。

## 配置项

- `CONFIG_ESPESP_I2C_SDA_GPIO`：I2C SDA GPIO。
- `CONFIG_ESPESP_I2C_SCL_GPIO`：I2C SCL GPIO。
- `CONFIG_ESPESP_I2C_PROBE_TIMEOUT_MS`：单个地址探测超时，单位 ms。

## 接线

默认配置：

- SDA GPIO8 -> 模块 SDA
- SCL GPIO9 -> 模块 SCL
- 3V3 -> 模块 VCC
- GND -> 模块 GND

## 日志现象

扫描到设备时会打印类似：

```text
found device at 0x3c
```

## 注意事项

- 扫到地址只代表 ACK，不代表设备寄存器协议正确。
- 长线或高速 I2C 更需要外部上拉。

## 扩展方向

- 接一个 OLED，观察常见地址 `0x3c`。
- 换 SDA/SCL 引脚并重新扫描。
- 调大 probe timeout，观察长线或弱上拉时的变化。
