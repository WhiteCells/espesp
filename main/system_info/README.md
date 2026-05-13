# system_info

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> system_info: chip/app/heap information
```

然后执行：

```sh
idf.py build flash monitor
```

## 当前模块已有接口

### `esp_err_t system_info_run(void)`

打印固件、芯片、Flash、堆内存和 Wi-Fi STA MAC 信息。

参数：无。

返回值：

- `ESP_OK`：所有信息读取成功。
- `esp_flash_get_size()` 或 `esp_read_mac()` 返回的错误。

## 常用接口说明

### `const esp_app_desc_t *esp_app_get_description(void)`

获取当前固件描述。

返回值：

- 指向 `esp_app_desc_t` 的指针，不需要释放。

常用字段：

- `project_name`：工程名，对应顶层 `project(espesp)`。
- `version`：应用版本，通常来自 git describe 或工程配置。
- `idf_ver`：构建该固件使用的 ESP-IDF 版本。

### `void esp_chip_info(esp_chip_info_t *out_info)`

读取芯片信息。

参数：

- `out_info`：输出结构体指针，不能为 `NULL`。

相关结构体 `esp_chip_info_t`：

- `model`：芯片型号枚举。
- `features`：能力位图，例如 Wi-Fi、BT、BLE、内置 Flash。
- `cores`：CPU 核心数量。
- `revision`：芯片 revision，代码中格式化成 `v主版本.次版本`。

### `esp_err_t esp_flash_get_size(esp_flash_t *chip, uint32_t *out_size)`

读取 Flash 容量。

参数：

- `chip`：Flash 句柄，传 `NULL` 表示默认主 Flash。
- `out_size`：输出字节数。

返回值：

- `ESP_OK`：读取成功。
- 其他错误表示 Flash 驱动读取失败。

### `esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type)`

读取 MAC 地址。

参数：

- `mac`：长度至少 6 字节的缓冲区。
- `type`：MAC 类型，本模块使用 `ESP_MAC_WIFI_STA`。

## 注意事项

- Flash 容量不等于 app 分区大小，app 分区由分区表决定。
- `heap_caps_get_largest_free_block(MALLOC_CAP_DMA)` 可以观察 DMA 可用连续内存，对 I2S/ADC 等模块有参考价值。
