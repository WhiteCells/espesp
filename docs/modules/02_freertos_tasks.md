# 02 freertos_tasks: FreeRTOS 任务与队列

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
  -> Demo selector
  -> 02 freertos_tasks
```

可调参数：

```text
Case2 ESP Learning
  -> FreeRTOS task demo
```

## 看哪段代码

- `main/demos/freertos_tasks_demo.c`

## 日志现象

- producer 按周期生成 sample。
- consumer 从队列读取 sample。
- heartbeat 每 3 秒输出一次堆内存。

## 练习

- 把 producer 周期改成 200 ms。
- 把队列长度改成 1，观察是否出现 drop。
- 给 consumer 加一个较长延时，观察队列满的情况。
