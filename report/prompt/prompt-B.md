# Lab4 B 技术协作摘要（整理稿）

## 技术目标与约束
实现 cpu_idle、schedule、proc_run、switch_to，适配必要的 Lab3 依赖。沿用既有接口和结构布局，正确处理 Sv39 页表及中断状态，保留官方测试。

## 验证与交接
五个文件独立编译通过，B 专项 QEMU 九项通过。合入 A 的 ef0499d 后，B 回归九项通过。
结果见 local-b-compile.log、local-b-runtime.log，合入前日志见 b-before-A-runtime.log。

专项使用手工 PCB 和页表夹具，正式创建链及完整工程仍待验收。Lab2 依赖和 C 页操作需要补齐；两个 Challenge 由 C 负责。
