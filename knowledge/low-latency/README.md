# low-latency 模块 — 低延迟系统编程

> 量化私募 JD 的核心加分项与面试区分轮（[research/01](../../research/01-jd-quant-cpp.md)、[research/04](../../research/04-interview-landscape.md) §2 低延迟编码轮）。

## 知识点清单

| 知识点 | 目录 | 状态 | 优先级 | 依据 |
|---|---|---|---|---|
| 伪共享与缓存行对齐 | [false-sharing/](false-sharing/README.md) | expanded | P0 | HFT 题库 Q2；JD「缓存行对齐」 |
| 内存序与原子操作 | memory-ordering/ | planned | P0 | HFT 题库 Q1/Q3/Q5 |
| 无锁结构：SPSC 队列、CAS、ABA | lock-free-structures/ | planned | P0 | HFT 题库 Q4-Q7 |
| 缓存友好数据布局（SoA vs AoS） | cache-friendly-layout/ | planned | P0 | HFT 题库 Q15 |
| 分支预测与 [[unlikely]]、内联权衡 | branch-and-inline/ | planned | P1 | HFT 题库 Q16-Q17 |
| 内存池与热路径分配规避 | memory-pool/ | planned | P1 | HFT 题库 Q20 |
| 延迟测量：rdtsc、直方图、尾延迟 | latency-measurement/ | planned | P1 | phantomcodeai 低延迟原则 |
| 内核旁路与网卡调优（onload/dpdk） | kernel-bypass/ | planned | P2 | 核心系统岗 JD |
| NUMA 感知 | numa-awareness/ | planned | P2 | phantomcodeai 低延迟原则 |

优先级定义见 [research/03-tech-stack-map.md](../../research/03-tech-stack-map.md)。

## 模块说明

- 本模块强调**动手实验**：每个知识点尽量给出可实测的基准 example（伪共享已有，SPSC 队列、内存池等展开时同样要求）。
- 与 [arch](../arch/README.md)（缓存/流水线原理）、[os](../os/README.md)（调度/系统调用成本）深度交叉；与 [cpp/memory-model 类真题](../cpp/README.md) 互链。
- 面试信条（phantomcodeai）：**热路径上的分配几乎总是 bug 而非优化机会；lock-free ≠ wait-free**。
