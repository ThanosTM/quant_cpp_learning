# 伪共享与缓存行对齐（False Sharing & Cache Line Alignment）

> 模块: low-latency ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-09-02 ｜ 更新: 2026-09-02
> 依据: HFT 题库 Q2；JD 明确点名「缓存行对齐」（[research/03](../../research/03-tech-stack-map.md)）

## 1. 一句话定位

伪共享指两个线程频繁写**同一条缓存行上的不同变量**，引发缓存行在核间反复失效同步，把本无竞争的代码拖慢数倍到数十倍；解法是让热点变量各自独占一条缓存行（`alignas`）。

## 2. 为什么面试官爱问

- 一句话就能筛人："两个线程各写各的计数器，为什么比单线程还慢？"——答案只有懂缓存一致性的人能给出。
- 修复手段（`alignas(64)`）极简单，但**为什么 64、为什么多核会退化、MESI 怎么工作**需要体系结构功底，是低延迟轮的经典区分题（[research/04](../../research/04-interview-landscape.md)）。

## 3. 前置知识

- 缓存层次结构（arch/cache-hierarchy-mesi/，规划中）
- `std::atomic` 基础（low-latency/memory-ordering/，规划中）

## 4. 核心讲解

### 4.1 缓存以"行"为单位搬运

CPU 缓存与内存交换的最小单位是缓存行（cache line）：x86 主流为 **64 字节**，Apple Silicon（M 系列）为 **128 字节**。命中与否、失效与否都按整行处理，而不是按变量。

### 4.2 MESI：一致性以行为粒度

MESI 协议给每个缓存行标记 Modified / Exclusive / Shared / Invalid 四态。核心规则：

- 多核可以同时**读**同一行（都为 S 态）；
- 任何核要**写**某行，必须先让其他核的副本全部失效（本核转 M 态，独占）。

于是本例的灾难现场：核 A 写 `a`、核 B 写 `b`，而 `a`、`b` 同行——

```
核A写a → 全核失效该行 → 核B写b(取行,M态) → 核A再写(该行已失效,重新取) → ...
        "缓存行乒乓"（cache line ping-pong）：每次写都是跨核一致性流量
```

实测影响可达 **10-100x**（[quantt Q2](https://www.quantt.co.uk/resources/cpp-quant-interview-questions)）；本目录 `example.cpp` 可直接复现数倍差距。

### 4.3 修复：让热点变量独占缓存行

```cpp
struct Padded {
    alignas(64) std::atomic<std::uint64_t> a{0};
    alignas(64) std::atomic<std::uint64_t> b{0};
};
```

- `alignas(64)` 保证成员起始地址 64 对齐，相邻成员不会落进同一行。
- 跨平台更稳的做法：Apple Silicon 行宽 128B，可 `alignas(128)`（本仓库 example 即采用并解释原因）；C++17 起可用 `std::hardware_destructive_interference_size`（需实现支持）。
- 同族手段：结构体手动 padding、热/冷字段分离（把只读字段与多线程写字段分开放置）与每线程局部聚合后再合并（sharding）。

### 4.4 反向思考：什么时候"共享同一行"反而是优化

多个线程**只读**同一行毫无问题（S 态共享）；需要的是"避免**写-写**同行"。另外不要过度 padding：盲目对齐会浪费缓存容量、破坏结构体紧凑性——先测量（`perf c2c` 可直接定位伪共享；macOS 可用 Instruments）再优化。

## 5. Example 说明

```bash
g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
```

预期观察点：两个线程各 `fetch_add` 2000 万次（relaxed），

- `Unpadded`（两计数器同行）：明显更慢（常见 3-10x，取决于核拓扑与行宽）；
- `Padded`（各占 128B）：接近无竞争的纯原子加；
- 输出含 `sizeof` 对比与加速比。注意多核机器间波动较大，建议跑 3 次取中位数；`taskset` 绑核可减小方差（Linux）。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| 什么是伪共享？ | 不同线程写同缓存行上的不同变量，MESI 迫使行在核间失效/重取，写操作退化为跨核同步 |
| 缓存行多大？ | x86 常见 64B；Apple Silicon 128B；用 `alignas` 让写热点独占一行 |
| 怎么定位伪共享？ | Linux `perf c2c`；代码审查找"相邻的跨线程写变量"；先基准测量再改 |
| lock-free 队列为什么 head/tail 也要 alignas？ | 生产者写 tail、消费者写 head，同行即自造伪共享——SPSC 队列实现的标准细节 |
| MESI 四态含义？ | M 已改独占 / E 独占干净 / S 多核共享 / I 失效；写前须把他人副本变 I |

## 7. 深度追问记录（落盘区）

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|
| 2026-09-02 | 为什么用 128 对齐而不是 64？ | x86 行宽 64B，64 对齐已隔离；Apple Silicon 行宽 128B，两个 64B 对齐成员（偏移 0 与 64）仍同行，128 才跨平台保险；C++17 亦可查询 hardware_destructive_interference_size | 是（§4.3 注明 example 采用 128 及原因） |
| 2026-09-02 | atomic 变量放在同一行也会伪共享吗？plain uint64_t 呢？ | 会。伪共享是缓存行层面的现象，与是否 atomic 无关；atomic 只是把"写"变成必然穿透缓存一致性的写（x86 lock 前缀），同行 atomic 写的同步惩罚通常更重。plain 变量同样发生行失效，只是单核上编译器可能先把累加优化进寄存器，掩盖现象 | 无（§4.2 已隐含，此处存档） |

## 8. 参考

- [quantt Q2 False sharing](https://www.quantt.co.uk/resources/cpp-quant-interview-questions)（核实日期 2026-09-02）
- cppreference: [alignas](https://en.cppreference.com/w/cpp/language/alignas) / [hardware interference sizes](https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size)
- 后续展开联动：`arch/cache-hierarchy-mesi/`（MESI 详解）、`low-latency/lock-free-structures/`（SPSC 中 alignas 的实战）
