# 09 i2c_scan: I2C 总线扫描

## 学什么

- `i2c_new_master_bus()` 创建 I2C master bus。
- `i2c_master_probe()` 检查地址是否 ACK。
- I2C 地址通常是 7-bit 地址。
- SDA/SCL 需要上拉。

## 怎么运行

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Demo selector
  -> 09 i2c_scan
```

可调参数：

```text
Case2 ESP Learning
  -> I2C scan demo
```

## 看哪段代码

- `main/demos/i2c_scan_demo.c`

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

## 练习

- 接一个 OLED，观察常见地址 `0x3c`。
- 换 SDA/SCL 引脚并重新扫描。
- 调大 probe timeout，观察长线或弱上拉时的变化。
