# os 模块 — 操作系统

> JD 明确要求 Linux 与操作系统原理（进程调度、内存管理）（[research/01](../../research/01-jd-quant-cpp.md)）；低延迟系统的很多「怪现象」（尾延迟、抖动）根源都在 OS。

## 知识点清单

| 知识点 | 目录 | 状态 | 优先级 | 依据 |
|---|---|---|---|---|
| 进程 / 线程 / 协程、fork 与写时复制 | process-thread-coroutine/ | planned | P0 | 面试必备 |
| 虚拟内存：页表 / TLB / 缺页中断 | virtual-memory-tlb/ | planned | P0 | 面试必备；HFT 题库 |
| 上下文切换成本、调度器与 CPU 亲和性 | context-switch-affinity/ | planned | P0 | 低延迟高频题 |
| 内存分配器：malloc 底层 / glibc / tcmalloc / jemalloc | memory-allocator/ | planned | P0 | HFT 题库 Q20；JD「内存管理」 |
| 系统调用路径、用户态/内核态切换、vDSO | syscall-vdso/ | planned | P1 | 低延迟实战（时钟获取为何快） |
| 文件 IO 与 page cache：write / fsync / mmap | file-io-page-cache/ | planned | P1 | 行情落盘与日志场景 |

优先级定义见 [research/03-tech-stack-map.md](../../research/03-tech-stack-map.md)。

## 模块说明

- 面试特征：喜欢问「量级」——一次系统调用多少纳秒、一次上下文切换多少微秒、一次缺页多少开销；能把数字说对就是区分度。
- 领域交叉：调度与亲和性 → `low-latency/numa/`、`low-latency/latency-measurement/`；malloc → `cpp/allocators/`。
- 典型追问链示例：malloc 怎么工作 → brk/mmap 阈值 → 为什么要内存池 → tcmalloc 怎么做到无锁 → 与 NUMA 的关系。
