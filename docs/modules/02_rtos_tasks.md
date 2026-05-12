# 02 rtos_tasks: FreeRTOS 任务与队列

## 学什么

- `xTaskCreate()` 创建任务。
- `xQueueCreate()` 创建队列。
- `xQueueSend()` 从 producer 发送数据。
- `xQueueReceive()` 在 consumer 阻塞等待数据。
- `vTaskDelay()` 按 tick 延时并让出 CPU。

## 怎么运行

```text
idf.py menuconfig
  -> Case2 ESP Learning
  -> Module selector
  -> 02 rtos_tasks
```

可调参数：

```text
Case2 ESP Learning
  -> FreeRTOS task module
```

## 看哪段代码

- `main/rtos_tasks/rtos_tasks.c`

## 接口介绍

- `rtos_tasks_run()`：创建三个任务和一个队列。
- 常用接口：`xTaskCreate()`、`xQueueCreate()`、`xQueueSend()`、`xQueueReceive()`。

## 日志现象

- producer 按周期生成 sample。
- consumer 从队列读取 sample。
- heartbeat 每 3 秒输出一次堆内存。

## 注意事项

- 队列长度过短或 consumer 处理过慢时会丢 sample。
- 真实业务要根据 stack high water mark 调整任务栈大小。

## 练习

- 把 producer 周期改成 200 ms。
- 把队列长度改成 1，观察是否出现 drop。
- 给 consumer 加一个较长延时，观察队列满的情况。
