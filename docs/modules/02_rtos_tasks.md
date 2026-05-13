# 02 rtos_tasks: FreeRTOS 任务与队列

## 模块概览

- `xTaskCreate()` 创建任务。
- `xQueueCreate()` 创建队列。
- `xQueueSend()` 从 producer 发送数据。
- `xQueueReceive()` 在 consumer 阻塞等待数据。
- `vTaskDelay()` 按 tick 延时并让出 CPU。

## 使用方式

```text
idf.py menuconfig
  -> ESPESP Menu
  -> Module selector
  -> 02 rtos_tasks
```

可调参数：

```text
ESPESP Menu
  -> FreeRTOS task module
```

## 源码位置

- `main/rtos_tasks/rtos_tasks.c`

## 当前模块接口参考

- `rtos_tasks_run()`：创建三个任务和一个队列。
- `producer_task()`：周期生成 `sensor_sample_t` 并写入队列。
- `consumer_task()`：阻塞读取队列并打印 sample。
- `heartbeat_task()`：周期打印堆内存和任务存活日志。

## 常用接口说明

- `xQueueCreate()`：创建固定长度队列，元素大小必须和发送数据结构一致。
- `xTaskCreate()`：创建 FreeRTOS 任务，配置任务入口、栈大小、优先级和参数。
- `xQueueSend()`：向队列发送数据，可设置等待时间处理队列满。
- `xQueueReceive()`：从队列读取数据，可阻塞等待生产者发送。
- `vTaskDelay()`：按 tick 延时并让出 CPU。
- `uxTaskGetStackHighWaterMark()`：观察任务剩余栈水位，辅助调整栈大小。

## 配置项

- `CONFIG_ESPESP_FREERTOS_QUEUE_LENGTH`：producer 和 consumer 之间的队列长度。
- `CONFIG_ESPESP_FREERTOS_PRODUCER_PERIOD_MS`：producer 生成 sample 的周期，单位 ms。

## 日志现象

- producer 按周期生成 sample。
- consumer 从队列读取 sample。
- heartbeat 每 3 秒输出一次堆内存。

## 注意事项

- 队列长度过短或 consumer 处理过慢时会丢 sample。
- 真实业务要根据 stack high water mark 调整任务栈大小。

## 扩展方向

- 把 producer 周期改成 200 ms。
- 把队列长度改成 1，观察是否出现 drop。
- 给 consumer 加一个较长延时，观察队列满的情况。
