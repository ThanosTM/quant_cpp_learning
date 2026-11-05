# arch 模块 — 计算机体系结构

> JD 明确点名 x86/ARM、缓存行对齐、SIMD、分支预测（[research/01](../../research/01-jd-quant-cpp.md)）；「代码为什么快/慢」的最终解释权都在这一层。

## 知识点清单

| 知识点 | 目录 | 状态 | 优先级 | 依据 |
|---|---|---|---|---|
| 缓存层次结构、局部性、MESI | [cache-hierarchy-mesi/](cache-hierarchy-mesi/README.md) | expanded | P0 | JD 点名；false-sharing 的前置 |
| 分支预测与预测失败惩罚 | [branch-predictor/](branch-predictor/README.md) | expanded | P0 | 经典 example：有序/无序数组遍历；branchless 对比 |
| 流水线、乱序执行与 IPC | [pipeline-superscalar/](pipeline-superscalar/README.md) | expanded | P1 | 面试追问「CPU 是怎么跑快的」；依赖链 vs ILP |
| SIMD：SSE / AVX / NEON 与自动向量化 | [simd/](simd/README.md) | expanded | P1 | JD 点名 SIMD |
| 硬件/软件预取与预取提示 | prefetch/ | planned | P2 | 数据布局进阶 |
| CPU 拓扑：超线程、P/E 核（Apple Silicon） | cpu-topology-p-cores/ | planned | P2 | 跨平台基准测试需知 |
| 计时与计数器：rdtsc / clock_gettime / 性能计数器 | [timing-counters/](timing-counters/README.md) | expanded | P2 | 与 low-latency/latency-measurement 交叉；arch 主线第 0 站 |

优先级定义见 [research/03-tech-stack-map.md](../../research/03-tech-stack-map.md)。

## 体系结构主线（建议学习顺序）

> 思路：面试题以「现象」出现，所以按「先能测 → 再解释缓存/分支 → 再解释并行度 → 最后补链」推进；每个知识点都必须产出一组本机可复现的量化差异。

| 顺序 | 知识点 | 优先级 | 这一站解决什么 | 实验锚点（example 选题） |
|---|---|---|---|---|
| 0 | [timing-counters](timing-counters/README.md) ✅ 已展开 | P2 | 建立可信的 cycle/ns 基线，否则后面所有倍速都是噪声 | `clock_gettime`/`std::chrono` 开销；ARM 计数器实测 + x86 `rdtsc` 机制对照 |
| 1 | [cache-hierarchy-mesi](cache-hierarchy-mesi/README.md) ✅ 已展开 | P0 | 行优先 vs 列优先的数量级差异从哪来；伪共享的机制根 | 工作集延迟曲线；2048×2048 矩阵两种遍历；MESI 偏移扫描 |
| 2 | [branch-predictor](branch-predictor/README.md) ✅ 已展开 | P0 | 有序/无序数组的条件求和差异；branchless 何时更快 | 有序/交替/随机三种模式；branch vs branchless；汇编确认真分支 |
| 3 | [pipeline-superscalar](pipeline-superscalar/README.md) ✅ 已展开 | P1 | 依赖链 vs 指令级并行；为什么多开累加器能提速 | 1/2/4/8 累加器归约；纯 ALU 链 1 vs 8；汇编确认标量 fadd |
| 4 | [simd](simd/README.md) ✅ 已展开 | P1 | 自动向量化何时生效/失效；NEON 与 AVX 的对照 | 乘法三版本（标量/auto/NEON）、点积 SIMD×ILP 五版本、工作集扫描 + `-S` 汇编核对；`-O2` vs `-O3 -march=native` |
| 5 | [prefetch](prefetch/)（planned） | P2 | 硬件预取的边界，软件预取的收益与反噬 | 顺序/随机遍历 ± `__builtin_prefetch` |
| 6 | [cpu-topology-p-cores](cpu-topology-p-cores/)（planned） | P2 | P/E 核、超线程、绑核如何污染基准测试 | 同负载下的 QoS/绑核/迁移对比 |

- 顺序理由：0 是测量地基（虽标 P2，但不先解决它，后面所有实验不可信）；1→2 是 P0 面试高频；3→4 解释「为什么代码改一行就快几倍」；5→6 属于补链，时间紧可后置。
- 与已展开知识点的衔接：先读 [false-sharing](../low-latency/false-sharing/README.md) 的现象，再用第 1 站补 MESI 机制；第 1 站与 [virtual-memory-tlb](../os/virtual-memory-tlb/README.md) 联读（cache/TLB 同属存储层次）；第 6 站与 [context-switch-affinity](../os/context-switch-affinity/README.md) 联读（绑核与 QoS）。
- 第 3 站 → 第 4 站的关键结论：ILP 与 SIMD 正交且可叠加；本机实测标量 4 累加器 5.32x、单 NEON 累加器 4.04x、SIMD+ILP 13.22x（见 [simd §4.7](simd/README.md)）。
- 每站验收（AGENT.md §2.A）：README 八节 + `example.cpp` 编译运行通过 + §5 写入本机实测数字（3 次中位数）+ ≥3 条追问 + 5 题自测 → `mastered`；真题拆到 `interview/system/` 并双向互链。

## 本机实验环境注意（Apple M2 Pro / macOS）

- `hw.cachelinesize=128B`（x86 常见 64B）；本站实测写-写干扰在 64B 边界消失，padding 仍建议保守用 128B，正文已记录该差异。
- 无 SSE/AVX，SIMD 以 NEON 与自动向量化为主；`simd/example.cpp` 用 `#if defined(__ARM_NEON) || defined(__aarch64__)` 分支，并附 SSE2 / AVX2 等价写法（x86 分支未在本机编译验证）。
- 本机 `-O2` 已开启自动向量化，`-O3 -march=native` 实测几乎无额外收益；核对是否向量化必须看 `-S` 汇编（见 [simd §5](simd/README.md)）。
- 无 Linux `perf`，用 Instruments（Time Profiler / CPU Counters）或自建计数器；`rdtsc` 不可用，ARM 侧用 `mach_absolute_time`/`cntvct_el0`。
- P/E 核与频率波动会放大方差：用线程 QoS/优先级尽量落在 P 核，跑 3 次取中位数；跨平台结论标注「本机实测」。

## 模块说明

- 面试特征：体系结构题往往以「现象」出现（为什么遍历有序数组更快、为什么这个循环改一行就快 5 倍），考察的是「现象 → 微架构机制 → 代码对策」的完整链路。
- 领域交叉：MESI 是 `low-latency/false-sharing/` 的直接前置；SIMD 与编译器优化在 `low-latency/branch-and-inline/` 也会涉及。
- 典型追问链示例：分支预测 → 预测失败惩罚多少周期 → 为什么循环内 if 难优化 → branchless 写法 → 与无序 map 哈希探测的关系。
