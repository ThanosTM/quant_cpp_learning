# os 模块 — 操作系统

> JD 明确要求 Linux 与操作系统原理（进程调度、内存管理）（[research/01](../../research/01-jd-quant-cpp.md)）；低延迟系统的很多「怪现象」（尾延迟、抖动）根源都在 OS。

## 知识点清单

| 知识点 | 目录 | 状态 | 优先级 | 最近更新 | 依据 |
|---|---|---|---|---|---|
| 进程 / 线程 / 协程、fork 与写时复制 | process-thread-coroutine/ | planned | P0 | — | 面试必备 |
| 虚拟内存：页表 / TLB / 缺页中断 | virtual-memory-tlb/ | planned | P0 | — | 面试必备；HFT 题库 |
| 上下文切换、调度器与 CPU 亲和性 | [context-switch-affinity/](context-switch-affinity/README.md) | expanded | P0 | 2026-11-03 | 低延迟高频题；JD「进程调度」 |
| 内存分配器：malloc 底层 / glibc / tcmalloc / jemalloc | memory-allocator/ | planned | P0 | — | HFT 题库 Q20；JD「内存管理」 |
| 系统调用路径、用户态/内核态切换、vDSO | syscall-vdso/ | planned | P1 | — | 低延迟实战（时钟获取为何快） |
| 中断、软中断与收包路径（IRQ affinity / NAPI） | interrupt-irq-affinity/ | planned | P1 | — | 低延迟网络前置；内核旁路之前必懂 |
| 内核抢占、PREEMPT_RT 与抖动源（C-state / 频率） | kernel-preemption-rt/ | planned | P1 | — | Linux 实时性主线；尾延迟 |
| 文件 IO 与 page cache：write / fsync / mmap | file-io-page-cache/ | planned | P1 | — | 行情落盘与日志场景 |

优先级定义见 [research/03-tech-stack-map.md](../../research/03-tech-stack-map.md)。

## 低延迟主线（建议学习顺序）

> 思路：先建立「成本直觉」，再补内核机制，最后落到隔离、调优与测量。

| 顺序 | 知识点 | 这一站解决什么问题 |
|---|---|---|
| 0（入口） | [context-switch-affinity](context-switch-affinity/README.md) ✅ 已展开 | 一次切换贵在哪、EEVDF/RT 怎么选任务、为什么绑核与隔离 |
| 1 | 进程 / 线程 / 协程（planned） | 线程、进程、协程的创建/切换成本与适用场景 |
| 2 | 虚拟内存与 TLB（planned） | 缺页、TLB miss、大页、`mlock` 如何进入尾延迟 |
| 3 | 系统调用与 vDSO（planned） | 一次 syscall 的完整路径，为什么读时钟不用进内核 |
| 4 | 中断 / IRQ 亲和性（planned） | 网卡收包从硬中断到 NAPI，谁在打扰你的关键核 |
| 5 | 内核抢占与 PREEMPT_RT（planned） | 抢占模型、RT throttling、C-state/频率与抖动 |
| 6 | 内存分配器（planned） | 热路径为什么不能 malloc，tcmalloc/jemalloc 怎么选 |
| 7 | 文件 IO 与 page cache（planned） | 行情落盘、日志与 `fsync` 的延迟代价 |

## 模块说明

- 面试特征：喜欢问「量级」——一次系统调用多少纳秒、一次上下文切换多少微秒、一次缺页多少开销；能把数字说对就是区分度。
- 领域交叉：调度与亲和性 → [low-latency/numa-awareness](../low-latency/README.md)、[low-latency/latency-measurement](../low-latency/README.md)；malloc → `cpp/allocators/`。
- 典型追问链示例：malloc 怎么工作 → brk/mmap 阈值 → 为什么要内存池 → tcmalloc 怎么做到无锁 → 与 NUMA 的关系。
