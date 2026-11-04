# 缓存层次结构、局部性与 MESI

> 模块: arch ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-03 ｜ 更新: 2026-11-04
> 依据: JD 点名「缓存行对齐」；[false-sharing](../../low-latency/false-sharing/README.md) 的机制根；arch 主线第 1 站（P0）。

## 1. 一句话定位

缓存层次决定「数据在哪一级、一次搬多少字节」，局部性决定程序能否把顺序访问变成流水，MESI 决定多核写同一缓存行时是否发生跨核所有权转移——三者合起来解释了「为什么改一行代码就快 10 倍」以及「为什么两个线程各写各的变量反而更慢」。

## 2. 为什么面试官爱问

- 现象题密度最高：矩阵行优先 vs 列优先、有序数组 vs 链表、两个线程各写各的计数器却更慢、`alignas` 到底该写 64 还是 128。
- 一句话就能分层：只会说「缓存快」的人会答「因为 cache 命中」；懂的人能说出 **line 粒度、局部性、MESI 状态、所有权转移、TLB** 的完整链路。
- 与低延迟岗位直接相关：订单簿/行情数据的布局、无锁队列的 head/tail padding、热字段分离、per-thread 计数器，全部建立在这一站。
- 后续 [`false-sharing`](../../low-latency/false-sharing/README.md) 已展开现象，本站补机制；[`cache-friendly-layout`](../../low-latency/README.md)（规划中）会用这里的结论做数据布局。

## 3. 前置知识

- [timing-counters](../timing-counters/README.md)（已展开）：所有实验的计时基线。
- [virtual-memory-tlb](../../os/virtual-memory-tlb/README.md)（已展开）：地址翻译与 TLB；cache 与 TLB 同属存储层次。
- [false-sharing](../../low-latency/false-sharing/README.md)（已展开）：先看现象，再看本站的 MESI 机制。
- [context-switch-affinity](../../os/context-switch-affinity/README.md)（已展开）：多线程实验的调度噪声来源。
- 下一站 [branch-predictor](../branch-predictor/README.md)（规划中）。

## 4. 核心讲解

### 4.1 层次结构：容量、延迟、行宽

本机（Apple M2 Pro）的硬件参数：

| 层级 | 容量 | 说明 |
|---|---|---|
| P-core L1D | 128 KB | `hw.perflevel0.l1dcachesize = 131072` |
| P-core L2 | 16 MB | `hw.perflevel0.l2cachesize = 16777216` |
| E-core L1D / L2 | 64 KB / 4 MB | `hw.perflevel1.*` |
| Cache line | 128 B | `hw.cachelinesize = 128` |

随机指针追逐（1,000,000 次依赖加载）实测的延迟台阶：

| 工作集 | ns/hop | 观察 |
|---|---|---|
| 4-128 KB | 约 1.2 | L1D 命中，基本是 1-2 个周期量级 |
| 256 KB-2 MB | 3.7-6.5 | 进入 L2 |
| 4-8 MB | 6.9-10.7 | L2 深处 |
| 16 MB | ~23 | 16 MB L2 边界 |
| 32-128 MB | 64-111 | L2 之外，DRAM 量级 |

要点：

- **缓存以行（line）为单位搬运**，不是以变量为单位。一次 miss 会把整行（本机 128B）拉进来；行内只要有一个字节被用到，其它字节就"免费"搭车。
- **容量与延迟不是线性关系**：跨过一级容量边界会出现台阶；曲线比单一数字更有说服力。
- **带宽与延迟是两件事**：顺序流式访问可以靠预取和并行 miss 隐藏延迟；随机指针追逐是延迟 bound，测的是单次访问的往返。
- 写策略通常是 write-back + write-allocate：写 miss 先把行拉进来，再在缓存里改，最后写回；这解释了「写一个字节也可能触发一次整行读取」。

#### 4.1.1 Cache 的物理结构：组、路、行、偏移

缓存不是一块扁平内存，而是 `sets × ways × line` 的二维阵列：

```text
地址:  [ tag | set index | offset ]
          |        |          |
          v        v          v
       tag 阵列   选组     行内字节
                  |
         way0..wayN 并行比较 tag
```

- `offset` 位数 = `log2(line size)`；
- `sets` 数 = `capacity / (line size × associativity)`；
- `index` 位数 = `log2(sets)`；
- 剩余高位是 `tag`。

以本机 128 KB、128 B 行、**假设** 8 路为例：lines = 1024，sets = 128，index = 7 bit，offset = 7 bit，64 位地址剩 50 bit tag（Apple 未公开实际相联度，8 路只用于算术示例）。

一次访问的流程：

1. 用 `set index` 选中一个组；
2. 该组所有 way 的 tag 并行比较，同时检查 valid/dirty；
3. tag 命中 → 按 offset 取字节；不命中 → 按替换策略选一个 way 驱逐，再发起下一级访问。

三种相联度：

| 类型 | 特点 | 代价 |
|---|---|---|
| 直接映射（1-way） | 每组只有一行，硬件最简单 | 冲突 miss 多 |
| 全相联（1 set） | 任意行可放任意位置 | 比较器/选择器昂贵，难做快 |
| 组相联（N-way） | 组内选路，折中 | 现代 L1/L2 主流 |

补充：

- **VIPT / PIPT**：L1 常用 virtually indexed, physically tagged——index 用虚拟地址提前选组，tag 必须等 TLB 翻译成物理地址后再比较；index 位数超过页内偏移时会有别名/页着色约束。
- **line 与 sector**：line 是取数/分配单位（本机 128B），sector 是有效性/一致性单位（本机实测 64B）；这解释了 §4.3 的边界现象。
- **替换策略**：LRU、伪 LRU、RRIP、随机；Apple 未公开具体实现，面试答「组相联 + 近似 LRU」即可，不要编造。

### 4.2 局部性：行优先为什么快 10 倍

矩阵 `m[i][j]` 在内存里按行连续存放。行优先遍历 `i` 外层、`j` 内层，访问地址连续，一条 128B 行能服务 16 个 `int64`；列优先遍历 `j` 外层、`i` 内层，每步跨 `n * 8` 字节：

```
行优先:  [0][0] [0][1] ... [0][15] | [0][16] ...   ← 同一行内连续
列优先:  [0][0] ------ 16KB ------ [1][0] ...      ← 每步换一条 line
```

本机实测（2048×2048，32 MB，禁用向量化）：

- 行优先 1.49 ms，列优先 15.26 ms，约 **10.2x**。
- 列优先每次只用 8B，却要搬 128B：**16 倍带宽放大**；步长 16 KB 还会持续打 TLB。
- 修复手段：循环交换（loop interchange）、分块（tiling/blocking，让子块塞进 L1/L2）、数据结构从 AoS 改 SoA（见 [low-latency/cache-friendly-layout](../../low-latency/README.md)，规划中）。

### 4.3 MESI：读共享没事，写共享要命

MESI 给每条缓存行四个状态：

| 状态 | 含义 |
|---|---|
| M（Modified） | 本核独占且已修改，数据与内存不一致 |
| E（Exclusive） | 本核独占且干净 |
| S（Shared） | 多核共享且干净 |
| I（Invalid） | 本核副本失效 |

核心规则：**多核可以同时读（S）；任何核要写，必须先让其它核的副本失效，自己拿到独占权（M）**。于是：

- 两个线程只读同一行 → 都停在 S，没有跨核流量，性能正常。
- 两个线程写同一行内**不同变量** → 每次写都要让对方失效、自己重新取行，缓存行在核间"乒乓"，这就是伪共享（false sharing）。
- 两个线程写**同一个变量** → 除了行失效，还要对同一个字做原子 RMW 串行化，是真共享（true sharing）。

#### 4.3.1 MESI 状态迁移

本地请求（读命中不变；写命中可能升级；只有 miss 才发起总线事务）：

| 当前状态 | 本地读 | 本地写 |
|---|---|---|
| M | M | M |
| E | E | **M（silent upgrade）** |
| S | S | **M（BusUpgr）** |
| I | 读 miss：无 sharer → E；有 sharer → S；他核为 M → S（owner 转发/写回） | 写 miss：BusRdX/RFO 拿所有权 → M |

snoop（其它核的请求，本核如何反应）：

| 当前状态 | 他人读（BusRd） | 他人写 / RFO（BusRdX） | 本核驱逐 |
|---|---|---|---|
| M | S + 写回/转发 | I + 写回 | I + 写回 |
| E | S | I | I（干净，直接丢） |
| S | S | I | I |
| I | I | I | — |

四条关键路径：

- **E → M 是静默升级**：本核独占且干净，没有其它副本，写命中不需要任何总线事务，是最快路径。
- **S → M 要 BusUpgr**：只发失效、不传数据；拿到独占权后才写。
- **M → S 要供数**：他人读命中脏行时，owner 必须转发数据或写回内存；MOESI 的 O 态可让 owner 保留脏行、避免立刻写回。
- **写 miss 要 RFO**：write-allocate 下，哪怕只写 1 字节，也要先把整行所有权拿过来；dirty 行驱逐时必须写回。

#### 4.3.2 嗅探 vs 目录，以及 MOESI/MESIF

- **嗅探（snooping）**：所有核监听共享总线/互连，看到 BusRd/BusRdX 就更新自己的状态。实现简单，但广播流量随核数增长，适合小规模 CMP。
- **目录（directory）**：home 节点维护 sharer/owner 位图，失效请求点对点发送。扩展性好，适合大规模多核/NUMA。
- **MOESI**：在 MESI 上增加 O（Owned）态——一个核持有脏行，同时允许其它核以 S 共享；避免 M→S 时立刻写回。AMD/ARM 常见。
- **MESIF**：增加 F（Forward）态——多个 S 副本中指定一个负责响应，减少重复转发；Intel 使用。
- **coherence ≠ consistency**：MESI 保证同一地址的写有一致顺序；跨地址的读写顺序由内存模型、fence、原子操作决定。store buffer 会让「写已提交」和「全局可见」之间存在窗口。

本机实测（2 线程 × 3,000,000 次 `fetch_add`，5 次取中位数）：

| 第二个计数器的偏移 | ns/op | 结论 |
|---|---|---|
| 0 B（同一变量） | 5.37 | 真共享，RMW 串行化 |
| 8 B（同一 64B sector） | 5.41 | 伪共享，行所有权来回转移 |
| 64 B（跨 64B sector） | 1.15 | 干扰基本消失 |
| 128 B（跨 128B 行） | 1.04 | 完全独立 |

**一个反直觉但重要的实测结果**：`sysctl hw.cachelinesize` 报告 128B，但写-写干扰在 **64B 边界**就消失了，而不是 128B。合理解释是：L1 行按 128B 取/预取，但一致性/所有权按 **64B sector** 跟踪（Apple 未公开微架构细节）。工程结论：

- 保守做法仍用 `alignas(128)`（跨平台、跨芯片最稳）；
- 但在 Apple Silicon 上不要假设「偏移 64B 就一定同一条一致性单元」——本机实测 64B 已经分开；
- 反过来，也不要因为 `hw.cachelinesize=128` 就以为 64B padding 一定无效，**以实测为准**。

修复伪共享的标准手段：`alignas(64/128)`、手动 padding、热/冷字段分离、per-thread 局部计数后合并（sharding）。定位手段：Linux `perf c2c`、macOS Instruments；代码审查找「相邻的跨线程写变量」。详见 [false-sharing](../../low-latency/false-sharing/README.md)。

### 4.4 相联度、替换与写策略（面试常追问）

- **相联度（associativity)**：直接映射（1-way）容易冲突 miss；组相联把地址映射到 set，组内用 LRU/伪 LRU 替换。矩阵列优先的 16 KB 步长还可能造成 set 冲突。
- **冲突 miss vs 容量 miss**：工作集没超过容量但映射到同一 set，叫冲突 miss；超过容量才是容量 miss。
- **写分配/写直达**：现代 CPU 多为 write-back + write-allocate；写 miss 先读整行。流式写可用非临时存储（NT store）绕过缓存，但属于进阶优化。
- **store buffer**：写不是立刻全局可见；这引出内存序问题（[low-latency/memory-ordering](../../low-latency/README.md)，规划中）。
- **MOESI**：部分 AMD/ARM 实现多一个 O（Owned）态，允许一个核持有脏行同时其它核共享，减少写回。

### 4.5 量化交易场景映射

- **订单簿**：价格档位数组按价格连续存放（数组/SoA），避免「每条订单一个对象、指针乱跳」；热字段（price/volume）与冷字段（订单 ID/时间）分离。
- **SPSC 队列**：生产者写 tail、消费者写 head；若二者同一行，就是自造伪共享——标准实现会把 head/tail 各自 `alignas` 到独立行。
- **行情分发**：按 symbol 分片（sharding），让每个线程写自己的计数器/统计，最后合并，避免全局原子计数器。
- **基准测试**：先测工作集曲线确认数据落在哪一级，再谈优化；不要凭感觉 padding，过度 padding 会浪费缓存容量、破坏紧凑性。

### 4.6 常见误区

- 「缓存行都是 64B」：x86 常见 64B；Apple `hw.cachelinesize` 报 128B，但一致性干扰可能在 64B 边界消失（见 §4.3）。
- 「列优先慢只是因为 TLB」：既有 line 利用率（16x 放大），也有 TLB 和预取失效，别只归因一个。
- 「伪共享只发生在锁/原子变量」：普通变量的跨线程写同样会触发行失效；`volatile` 也救不了。
- 「padding 越多越好」：每个热点变量独占一行会浪费容量，先测量再改。
- 「MESI 等于内存序」：MESI 管缓存一致性，内存序管读写可见顺序，两者是不同层。

## 5. Example 说明

```bash
g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
```

本机环境：Apple M2 Pro / macOS 26.5.1（Darwin 25.5.0）/ arm64 / Apple clang 17；3 次运行取中位数。

**实验一：工作集与访问延迟**

| 工作集 | ns/hop | 工作集 | ns/hop |
|---|---|---|---|
| 4 KB | 1.20 | 1 MB | 5.52 |
| 16 KB | 1.21 | 2 MB | 6.52 |
| 64 KB | 1.22 | 4 MB | 6.92 |
| 128 KB | 1.16 | 8 MB | 10.67 |
| 256 KB | 3.70 | 16 MB | 23.20 |
| 512 KB | 4.96 | 32 MB | 63.71 |
| | | 64 MB | 93.47 |
| | | 128 MB | 110.72 |

**实验二：矩阵遍历（2048×2048 int64，32 MB，禁用向量化）**

- 行优先 1.49 ms，列优先 15.26 ms，约 **10.2x**。
- 两遍求和结果相同（4194304），排除计算量差异。

**实验三：MESI 偏移扫描（2 线程 × 3,000,000 次 fetch_add）**

| 偏移 | ms | ns/op |
|---|---|---|
| 0 B（同一变量） | 32.24 | 5.37 |
| 8 B（同一 64B sector） | 32.45 | 5.41 |
| 64 B（跨 64B sector） | 6.87 | 1.15 |
| 128 B（跨 128B 行） | 6.25 | 1.04 |

观察点：

- L1 平台一直持续到 128 KB，256 KB 开始明显上升；16 MB 是 P-core L2 容量边界（`hw.perflevel0.l2cachesize=16 MB`），32 MB 之后进入 DRAM 量级。
- 矩阵列优先的 10x 差距同时来自 line 利用率（16x 放大）与 TLB/预取失效；实验禁用向量化，避免把 SIMD 差异混进来。
- MESI 实验证明本机写-写干扰在 64B 边界消失，尽管 `hw.cachelinesize=128`；线程 QoS 用于降低 P/E 核调度噪声，数值仍有约 10-20% 波动。
- `offset 0` 与 `offset 8` 的 ns/op 接近（5.37 vs 5.41）：两线程场景下真共享与伪共享都受一致性流量限制；线程数增加后真共享会更明显。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| 为什么行优先遍历比列优先快一个数量级？ | 行优先连续访问，128B 行服务 16 个 int64；列优先每步跨 16 KB，每 8B 就搬一整行，带宽放大 16 倍，还打 TLB、预取失效 |
| 缓存行多大？ | x86 常见 64B；Apple `hw.cachelinesize=128B`；但本机写-写干扰在 64B 边界消失，说明一致性按 64B sector 跟踪（推测） |
| MESI 四态是什么？ | M 独占已改、E 独占干净、S 多核共享、I 失效；写之前必须让其它副本失效 |
| 伪共享和真共享的区别？ | 伪共享是不同变量在同一一致性单元、各自写；真共享是同一变量，除了失效还要 RMW 串行化 |
| 为什么 `alignas(64)` 有时不够？ | Apple 行宽报告 128B；保守用 128B。但实测 64B 已能避开本机写干扰，最终以目标机实测为准 |
| 矩阵分块为什么有效？ | 把子块工作集压进 L1/L2，提高行内复用与 TLB 命中；典型 tile 32-64，按 cache 容量调 |
| 什么是冲突 miss？ | 工作集没超容量，但多个地址映射到同一 set，互相替换；增加相联度或改变步长可缓解 |
| 写一个字节为什么要读整行？ | write-back + write-allocate：写 miss 先把行拉入缓存再改；流式写可用 NT store 绕过 |
| 怎么定位伪共享？ | Linux `perf c2c`；macOS Instruments；或像本站一样做偏移扫描，观察 ns/op 在哪个字节边界跳变 |
| MESI 和内存序是一回事吗？ | 不是；MESI 保证缓存一致性，内存序保证读写可见顺序；后者属于 `memory-ordering` |

## 7. 深度追问记录（落盘区）

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|
| 2026-11-03 | 为什么 128 KB 工作集还在 1.2 ns，256 KB 就跳到 3.7 ns？ | P-core L1D 容量是 128 KB（`hw.perflevel0.l1dcachesize=131072`）；跨过容量边界进入 L2，延迟台阶约 3-5x。 | §4.1、§5 实验一 |
| 2026-11-03 | 为什么 `hw.cachelinesize=128`，但 offset 64B 就不再伪共享？ | 行按 128B 取/预取，但一致性/所有权可能按 64B sector 跟踪；Apple 未公开细节。保守 padding 仍用 128B，具体以实测为准。 | §4.3、§5 实验三 |
| 2026-11-03 | 列优先慢，是 line 利用率还是 TLB 的锅？ | 两者都有：每 8B 搬 128B 是 16x 带宽放大；16 KB 步长还会持续 TLB miss、预取失效。不能只归因一个。 | §4.2、§5 实验二 |
| 2026-11-03 | 为什么矩阵实验要禁用向量化？ | 行优先可自动向量化、列优先不能，会把 SIMD 差异混进 cache 差异；本站只研究存储层次，SIMD 留给第 4 站。 | §5 实验二 |
| 2026-11-04 | Cache 的地址如何切分成 tag/set/offset？MESI 状态如何迁移？ | 地址 = tag + set index + offset；sets = capacity/(line × ways)。MESI 迁移见 §4.3.1：E→M 静默升级、S→M 需 BusUpgr、M→S 需写回/转发、写 miss 需 RFO。 | §4.1.1、§4.3.1、§4.3.2 |

## 8. 参考

- Hennessy & Patterson, *Computer Architecture: A Quantitative Approach*，第 2 章（Memory Hierarchy Design）（核实日期 2026-11-03）
- Bryant & O'Hallaron, *CSAPP*，第 6 章（The Memory Hierarchy）（核实日期 2026-11-03）
- Ulrich Drepper, *What Every Programmer Should Know About Memory*：[PDF](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf)（核实日期 2026-11-03）
- Intel SDM Vol.3，缓存与内存子系统章节：[Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)（核实日期 2026-11-03）
- Linux `perf c2c` 文档：[perf wiki](https://perf.wiki.kernel.org/index.php/Main_Page)（核实日期 2026-11-03）
- 本机参数：`sysctl hw.cachelinesize hw.perflevel0.l1dcachesize hw.perflevel0.l2cachesize`（Apple M2 Pro，macOS 26.5.1，核实日期 2026-11-03）
- 关联知识点：[false-sharing](../../low-latency/false-sharing/README.md)、[virtual-memory-tlb](../../os/virtual-memory-tlb/README.md)、[timing-counters](../timing-counters/README.md)
- 关联真题：`interview/system/`（待补，缓存/伪共享类真题后续拆解归档）
