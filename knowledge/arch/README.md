# arch 模块 — 计算机体系结构

> JD 明确点名 x86/ARM、缓存行对齐、SIMD、分支预测（[research/01](../../research/01-jd-quant-cpp.md)）；「代码为什么快/慢」的最终解释权都在这一层。

## 知识点清单

| 知识点 | 目录 | 状态 | 优先级 | 依据 |
|---|---|---|---|---|
| 缓存层次结构、局部性、MESI | cache-hierarchy-mesi/ | planned | P0 | JD 点名；false-sharing 的前置 |
| 分支预测与预测失败惩罚 | branch-predictor/ | planned | P0 | 经典 example：有序/无序数组遍历 |
| 流水线、乱序执行与 IPC | pipeline-superscalar/ | planned | P1 | 面试追问「CPU 是怎么跑快的」 |
| SIMD：SSE / AVX / NEON 与自动向量化 | simd/ | planned | P1 | JD 点名 SIMD |
| 硬件/软件预取与预取提示 | prefetch/ | planned | P2 | 数据布局进阶 |
| CPU 拓扑：超线程、P/E 核（Apple Silicon） | cpu-topology-p-cores/ | planned | P2 | 跨平台基准测试需知 |
| 计时与计数器：rdtsc / clock_gettime / 性能计数器 | timing-counters/ | planned | P2 | 与 low-latency/latency-measurement 交叉 |

优先级定义见 [research/03-tech-stack-map.md](../../research/03-tech-stack-map.md)。

## 模块说明

- 面试特征：体系结构题往往以「现象」出现（为什么遍历有序数组更快、为什么这个循环改一行就快 5 倍），考察的是「现象 → 微架构机制 → 代码对策」的完整链路。
- 领域交叉：MESI 是 `low-latency/false-sharing/` 的直接前置；SIMD 与编译器优化在 `low-latency/branch-and-inline/` 也会涉及。
- 典型追问链示例：分支预测 → 预测失败惩罚多少周期 → 为什么循环内 if 难优化 → branchless 写法 → 与无序 map 哈希探测的关系。
