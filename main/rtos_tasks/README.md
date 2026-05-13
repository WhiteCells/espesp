# rtos_tasks

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> rtos_tasks: tasks and queue
```

可调参数：

```text
ESPESP Menu
  -> FreeRTOS task module
```

然后执行：

```sh
idf.py build flash monitor
```

## 当前模块已有接口

### `esp_err_t rtos_tasks_run(void)`

创建 producer、consumer、heartbeat 三个 FreeRTOS 任务，并用队列传递 `sensor_sample_t`。

参数：无。

返回值：

- `ESP_OK`：队列和任务创建成功。
- `ESP_ERR_NO_MEM`：队列或任务创建失败。

## 本模块结构体

### `sensor_sample_t`

模拟传感器采样数据。

字段说明：

- `sequence`：样本序号，每次 producer 生成样本后递增。
- `uptime_ms`：生成样本时的系统运行时间，单位 ms。

## 常用接口说明

### `QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)`

创建队列。

参数：

- `uxQueueLength`：队列可保存的元素个数，本模块来自 `CONFIG_ESPESP_FREERTOS_QUEUE_LENGTH`。
- `uxItemSize`：单个元素大小，本模块是 `sizeof(sensor_sample_t)`。

返回值：

- 非 `NULL`：队列句柄。
- `NULL`：内存不足。

### `BaseType_t xTaskCreate(TaskFunction_t pxTaskCode, const char *pcName, uint32_t usStackDepth, void *pvParameters, UBaseType_t uxPriority, TaskHandle_t *pxCreatedTask)`

创建任务。

参数：

- `pxTaskCode`：任务函数，例如 `producer_task`。
- `pcName`：任务名，用于调试。
- `usStackDepth`：任务栈大小，ESP-IDF 中单位是字节。
- `pvParameters`：传给任务函数的参数，本模块传 `NULL`。
- `uxPriority`：任务优先级，数字越大优先级越高。
- `pxCreatedTask`：输出任务句柄，本模块不需要，传 `NULL`。

返回值：

- `pdPASS`：创建成功。
- 其他：创建失败。

### `xQueueSend()` 和 `xQueueReceive()`

`xQueueSend(s_sample_queue, &sample, pdMS_TO_TICKS(100))`：

- 第一个参数是队列句柄。
- 第二个参数是要复制进队列的数据地址。
- 第三个参数是等待队列有空间的最长 tick 数。

`xQueueReceive(s_sample_queue, &sample, portMAX_DELAY)`：

- 第二个参数是接收缓冲区地址。
- `portMAX_DELAY` 表示一直等到收到数据。

## 可配置项

- `CONFIG_ESPESP_FREERTOS_QUEUE_LENGTH`：队列长度。
- `CONFIG_ESPESP_FREERTOS_PRODUCER_PERIOD_MS`：producer 生成样本周期。

## 注意事项

- 队列存的是数据副本，不是指针引用。
- producer 发送太快或 consumer 处理太慢时，`xQueueSend()` 会超时并丢弃样本。
