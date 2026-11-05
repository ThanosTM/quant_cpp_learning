# 002 — 订单簿：从 std::map 到线性搜索

> 类型: 技术文章复现 ｜ 状态: complete ｜ 创建: 2026-11-05 ｜ 更新: 2026-11-05
> 原文: [《Optiver 低延迟交易系统设计 10 原则（上）》](https://mp.weixin.qq.com/s/mYbS0pfrtIvy7slvd4eoAQ)（核实日期 2026-11-05）
> 原始演讲: CppCon 2024 — *When Nanoseconds Matter: Ultrafast Trading Systems in C++*（David Gross, Optiver）
> Optiver 文章: <https://optiver.com/working-at-optiver/career-hub/designing-low-latency-cpp-systems/>
> 平台: Apple M2 Pro / macOS 26.5.1 (Darwin 25.5.0, arm64) / Apple clang 17.0.0 / `-std=c++20 -O2 -Wall -Wextra -pthread`
> 代码: [src/main.cpp](src/main.cpp) ｜ 数据: [results/summary.md](results/summary.md)、[results/repeat_summary.md](results/repeat_summary.md)、[results/sweep_summary.md](results/sweep_summary.md)

## 1. 一句话结论

在 48 层买盘、200 万条头部主导的订单簿事件上，本机复现了原文最重要的两条结论：

- **基于节点的 `std::map` 在真实堆环境下很贵**：干净堆 p50 **18 ns**，在操作之间穿插 malloc/free 后升到 **55~59 ns**（约 3 倍），p99 从 **31~33 ns** 涨到 **95~129 ns**；原文是 33 → 63 ns。
- **小订单簿里线性搜索打败二分**：N=48 时 linear **13 ns** vs binary **15 ns**（-13%~-7%）；原文是 29 → 22 ns。
- **反转 vector 没有复现**：front/back 的 p50/p99 几乎一样（18/17 ns、33/30 ns），原文的长尾差异在 M2 上消失。
- **无分支二分没有复现**：`binary-branchy` 与 `binary-branchless` 的汇编完全相同（Clang 已把前者 if-convert 成 `csel`），A 组 p50 都是 15 ns，B 组 14 vs 15 ns；原文的 CMOV 收益在 ARM64 上不存在。
- **线性搜索的优势有明确边界**：深度扫描显示 N=32/48 时 linear 胜，N=64 打平，N=128 时 linear 25 ns vs binary 16 ns。原文没有强调这个 N 依赖，本机把它量化了出来。

## 2. 原文讲了什么

文章围绕订单簿的有序价格层级展开，按五次尝试总结调优原则：

| 尝试 | 数据结构 / 手段 | 原文结论 | 引出的原则 |
|---|---|---|---|
| 1 | `std::map<Price, Volume>` | 干净堆 33 ns，污染堆 63 ns，出现肥尾 | #1 大部分时候不需要节点容器 |
| 2 | 有序 `std::vector` + `lower_bound` | 中位数 34 ns，但有搬移长尾 | #2 观察数据，头部操作占绝大多数 |
| 3 | 把最优价放到 vector 尾部（反转顺序） | 头部插入变成 `push_back`，长尾消失 | — |
| 4 | 无分支二分（`comp * (length-half)`） | Bad Speculation 25% → 下降，IPC 1.4 → 1.57 | #3 特化算法 |
| 5 | 线性搜索 | 中位数 29 → 22 ns，分布更窄 | #4 简单是终极的复杂 |
| — | `[[likely]]` / cold code / lambda vs `std::function` | 关键路径瘦身 | #5 庖丁解牛 |

复现边界：微信文章没有给出完整代码、订单簿深度 N、事件分布与编译器参数；本实验是**按文字描述重建的代表性实现**，不是逐行移植。我们固定了单侧买盘、固定深度 N 和一套可复现的头部主导事件分布，以便把“数据结构差异”单独隔离出来。

## 3. 环境与计时

| 项 | 值 |
|---|---|
| CPU | Apple M2 Pro，12 核 = 8 P-core + 4 E-core |
| L1D / L2 | 128 KB / 16 MB 每 4 个 P-core 一个 cluster；无独立 L3 |
| 缓存行 | 128 B |
| 计时 | `mach_absolute_time`，24 MHz，41.67 ns/tick |
| PMU | 用户态不可用；没有 `perf` / Bad Speculation / IPC 计数器 |
| 编译器 | Apple clang 17.0.0，`-O2` |

计时限制决定了测量方式：单个订单簿操作只有 13~60 ns，而 timer 分辨率是 41.67 ns，逐操作计时会被量化成 0/1/2 tick。因此本实验采用**批次计时**：

```text
每 64 个操作读一次时钟，记录 (总时间 / 64) 作为一个样本；
对约 3 万个样本取 p50/p90/p99。
```

所以表中的 p50/p90 是“64 个操作平均后的分位数”，不是原文的单次操作分位数；绝对 ns 不能和原文逐位对比，但相对排序和数量级可以。

## 4. 案例模型

### 4.1 数据与事件

| 项 | 值 |
|---|---|
| 侧 | 买盘（bids）单侧，隔离掉买卖两侧的对称重复 |
| 最大深度 | 48 层（固定深度；满了以后新价格层级被忽略，等待删除腾位置） |
| 事件数 | 2 000 000（前 10% 预热，不计入统计） |
| 价格 | 从 1 000 000 tick 开始；每个事件相对当前最优价生成 |
| 事件大小 | 12 B：`{op, price, volume}` |
| 事件文件 | `build/events.bin`，24 MB，二进制头 + 事件流 |
| 校验和 | 最终 `{count, sum(price*volume), mix}`，七个结构必须完全一致 |

事件分布（头部主导）：

| 概率 | 操作 | 说明 |
|---|---:|---|
| 30% | `Set` 最优价 | 只改数量，不移动 |
| 30% | `Add` 最优价上方 1~8 tick | 新最优价；反转 vector 的 `push_back` 路径 |
| 10% | `Add` 盘口内 1~16 tick | 头部附近 |
| 5% | `Set` 深 17~256 tick | 深部插入，制造 vector 搬移 |
| 25% | `Delete` 最优价附近 0~8 tick | 让 best 向下移动；反转 vector 的 `pop_back` 路径 |

没有生成 `orderId -> price` 的哈希查找：本实验只比较“有序价格层级”本身。真实系统还要先查哈希表，但那部分对所有结构是同一个常数，会稀释差异。

### 4.2 七个变体

| 变体 | 布局 | 查找 |
|---|---|---|
| `map-clean` | `std::map<Price, Volume>` | `lower_bound`（红黑树） |
| `map-polluted` | 同上，操作之间穿插 malloc/free | 同上 |
| `vector-front` | 降序 vector，最优价在 `begin()` | `std::lower_bound` |
| `vector-back` | 升序 vector，最优价在 `back()` | `std::lower_bound` |
| `binary-branchy` | 升序 vector | 手写条件跳转二分 |
| `binary-branchless` | 升序 vector | `first += (v[mid] < p) * (len-half)` |
| `linear` | 升序 vector，从尾部向头部扫 | 线性搜索 |

`map-polluted` 的“污染”方式：在**每个操作之间**执行一次随机大小（16~128 B）的 `malloc`/`free`，与原文“在操作之间随机分配内存”一致；抖动开销计入延迟。`map-clean` 不做任何额外分配。

## 5. 五次尝试与实测

> A 组 = [results/summary.md](results/summary.md)，B 组 = 独立复跑的 [results/repeat_summary.md](results/repeat_summary.md)。每组 3 次运行取中位数。

### 5.1 第一次尝试：std::map

| 变体 | A 组 p50 / p90 / p99 / mean | B 组 p50 / p90 / p99 / mean |
|---|---:|---:|
| map-clean | **18 / 24 / 31 / 17.3** | **17 / 23 / 33 / 17.3** |
| map-polluted | **55 / 74 / 129 / 59.9** | **59 / 72 / 95 / 61.6** |

**解读**：

- 干净堆下 `std::map` 的中位数并不难看（17~18 ns）；原文也是 33 ns，和 vector 差不多。
- 一旦操作之间持续发生分配/释放，p50 升到 55~59 ns（约 3.1~3.5 倍），p99 升到 95~129 ns；原文 33 → 63 ns，方向完全一致。
- 原因不是“红黑树复杂度”，而是：节点独立分配、指针追逐、堆碎片。冻结的 `map` 节点只有 48 个，仍然能放进 L1，但节点地址在污染堆里变得不连续，预取器和缓存行都不帮忙。

### 5.2 第二次尝试：有序 vector

| 变体 | A 组 p50 / p90 / p99 / mean | B 组 p50 / p90 / p99 / mean |
|---|---:|---:|
| vector-front（最优价在头） | **18 / 21 / 33 / 17.4** | **18 / 22 / 27 / 17.6** |
| vector-back（最优价在尾） | **17 / 21 / 30 / 16.8** | **18 / 22 / 27 / 17.4** |

**解读**：

- 干净堆的 vector 与 map 的 p50 只差 0~1 ns，和原文“vector 34 ns vs map 33 ns”一致：**节点容器的问题主要在污染环境，不在干净环境**。
- 反转顺序（front → back）在本机没有带来可见收益：p50 18 → 17（A）、18 → 18（B），p99 33 → 30、27 → 27。原文说反转后长尾消失，本机**没有复现**。
- 原因是本机订单簿只有 48 层，头部插入搬移约 48×8 B = 384 B；M2 的 store/load 带宽和 `memmove` 让这个搬移非常便宜。要看到原文的长尾，需要更大的 N 或更慢的内存；但 N 一大，线性搜索又会输给二分（见 §6）。
- 这不是“反转无效”，而是“反转的收益被 M2 的搬移带宽吃掉了”。它是一个典型的“必须在本机重测”的参数。

### 5.3 第四次尝试：无分支二分

| 变体 | A 组 p50 / p90 / p99 / mean | B 组 p50 / p90 / p99 / mean |
|---|---:|---:|
| binary-branchy | **15 / 20 / 27 / 15.1** | **14 / 18 / 23 / 14.3** |
| binary-branchless | **15 / 19 / 26 / 14.8** | **15 / 19 / 27 / 15.0** |

**解读**：本机**没有复现**无分支二分的收益。A 组 p50 相同（15），B 组 branchless 反而慢 1 ns。原因可以直接从汇编看到：Clang 已经把条件跳转版本 if-convert 成 `csel`，两个源文件的机器码完全相同：

```asm
_search_branchy:
    ...
    cmp     w10, w2
    csel    x10, x11, xzr, lt
    add     x8, x10, x8
    cmp     x1, #1
    mov     x1, x9
    b.hi    LBB0_1

_search_branchless:
    ...                 ; 与上面逐条相同
```

完整汇编见 [results/search_asm.s](results/search_asm.s)，源文件见 [micro/search_asm.cpp](micro/search_asm.cpp)。原文的 CMOV 技巧在 x86 上有效，但在 ARM64 + Clang 上属于“源码层面重写、机器码不变”。同时，macOS 没有 PMU，原文的 Bad Speculation / IPC 1.4→1.57 无法在本机验证。

### 5.4 第五次尝试：线性搜索与深度边界

主实验 N=48：

| 变体 | A 组 p50 / p90 / p99 / mean | B 组 p50 / p90 / p99 / mean |
|---|---:|---:|
| binary-branchy | 15 / 20 / 27 / 15.1 | 14 / 18 / 23 / 14.3 |
| linear | **13 / 16 / 21 / 13.5** | **13 / 16 / 29 / 14.1** |

线性搜索在 N=48 时胜出（p50 -13% / -7%），方向复现原文的 29 → 22 ns。但在深度扫描中，优势很快消失：

| N | map-clean | vector-front | vector-back | binary-branchy | linear |
|---:|---:|---:|---:|---:|---:|
| 32 | 17 | 14 | 14 | 15 | **12** |
| 48 | 18 | 18 | 17 | 15 | **13** |
| 64 | 16 | 14 | 14 | 16 | 16 |
| 128 | 17 | 14 | 14 | 16 | 25 |

**解读**：

- N=32/48：线性搜索从尾部开始，绝大多数操作只扫 1~8 个元素，顺序访问 + 预取器占优，赢 13%~20%。
- N=64：线性与二分打平。
- N=128：线性需要扫更多元素，二分 7 次随机但可预测的访问反而更好；linear 25 ns vs binary 16 ns。
- 原文说“对于足够小的 N，线性搜索会赢”，但没有给出 N；本机把交叉点量化在 **48~64 层**。这是一个比“线性永远更快”更准确的结论。
- 注意：N=64/128 时 vector-front 和 vector-back 都是 14 ns，反转顺序完全没有差异；再次说明 M2 上 4 KB 级别的搬移太便宜。

## 6. 结果汇总

### 6.1 主实验（N=48，2M 事件）

| 变体 | A p50 | A p99 | B p50 | B p99 | 结论 |
|---|---:|---:|---:|---:|---|
| map-clean | 18 | 31 | 17 | 33 | 干净堆下不慢 |
| map-polluted | 55 | 129 | 59 | 95 | 节点容器在真实堆下显著退化 |
| vector-front | 18 | 33 | 18 | 27 | 与 map 干净堆接近 |
| vector-back | 17 | 30 | 18 | 27 | 反转收益在 M2 上很弱 |
| binary-branchy | 15 | 27 | 14 | 23 | 二分基线 |
| binary-branchless | 15 | 26 | 15 | 27 | 与 branchy 相同（汇编相同） |
| linear | **13** | **21** | **13** | 29 | N=48 时最快 |

### 6.2 与原文的对照

| 原文结论 | 原文数据 | 本机数据 | 是否复现 | 原因 |
|---|---|---|---|---|
| map 干净堆 33 ns | 33 ns | 17~18 ns | 量级不同，方向一致 | M2 单核更强、L1 更大；绝对 ns 不可直接比 |
| map 污染堆 63 ns、肥尾 | 33 → 63 ns | 18 → 55~59 ns，p99 95~129 | **复现** | 节点独立分配 + 堆碎片 |
| vector 34 ns，有长尾 | 34 ns | 17~18 ns | 方向一致 | 本机干净堆下 vector/map 都很快 |
| 反转 vector 消除长尾 | 图形结论 | front/back p50/p99 基本相同 | **未复现** | 48 层搬移仅 ~384B，M2 memmove 太快 |
| 无分支二分降 Bad Speculation、IPC 1.4→1.57 | 有 PMU 证据 | branchy/branchless 汇编完全相同，p50 相同 | **未复现** | Clang 已把 branchy if-convert 成 `csel`；无 PMU |
| 线性搜索 29 → 22 ns | 更快、更窄 | N=48：13 vs 15；N=128：25 vs 16 | **部分复现** | 优势依赖 N，交叉点约 48~64 |

## 7. 偏差、失败案例与局限

### 7.1 失败案例

1. **反转 vector**：没有出现原文的长尾差异。本机的 `vector-front` 和 `vector-back` 在 N=32/48/64/128 的 p50 都只差 0~1 ns。可能原因：订单簿太浅（最大 128 层 = 1 KB），`memmove` 吞吐太高；原文的订单簿可能更深，或运行在更慢的 i9 内存路径上。
2. **无分支二分**：源码改写了，机器码没变。Clang 对条件跳转版已经生成 `csel`；再写 `comp * (length-half)` 反而增加乘法依赖。A 组 p50 相同，B 组 branchless 慢 1 ns。
3. **`map-clean` vs `vector-back`**：A 组 18 vs 17（-1 ns），B 组 17 vs 18（+1 ns），没有稳定的 vector 优势。原文也是 33 vs 34；说明“节点容器更慢”主要体现在污染环境，而不是干净环境。
4. **线性搜索不是永远更快**：N=128 时 linear 25 ns vs binary 16 ns。原文的“简单是终极的复杂”必须带上 N 前提。

### 7.2 为什么 map-polluted 变化这么大

- 每个操作之间执行一次 `malloc`/`free`，制造 16~128 B 的空洞；`std::map` 的红黑树节点约 48 B，正好落在这些空洞里。
- 节点地址从“连续分配”变成“散落在 200k 个空洞中”，指针追逐的每次节点访问都可能落到不同缓存行。
- p50 从 18 → 55~59 ns，p99 从 31~33 → 95~129 ns；这正是原文蓝色分布的“肥尾”。
- 这个数字包含了 malloc/free 本身的开销，和原文“在操作之间随机分配内存”的测试口径一致；它不是纯 `map::lower_bound` 的微基准。

### 7.3 局限

1. **无 PMU**：不能测 Bad Speculation、IPC、L1/L2 命中率；只能用汇编 + 端到端延迟 + 深度扫描间接判断。
2. **批次计时**：p50/p90 是 64 个操作的块平均，不是单次操作分位数；它会平滑掉块内最慢的一两个操作。原文的单次 p50 不能逐位对比。
3. **合成事件分布**：头部主导分布是人为设定的（30/30/10/5/25），不是真实市场 replay；不同交易所、不同标的的分布会改变交叉点。
4. **单侧买盘**：没有建模买卖两侧、价差、订单队列；这些会给真实系统增加固定开销，但不改变有序结构的相对排序。
5. **固定深度 48**：满了以后忽略新价格，而不是删除最深层级；这避免了“深度裁剪”本身的搬移开销，但也让反转 vector 的收益更小。
6. **分配器差异**：macOS libc++/malloc 与 Linux glibc、jemalloc、mimalloc 的碎片行为不同；`map-polluted` 的 3 倍退化不能直接外推到生产环境的其他分配器。
7. **单机单进程**：没有绑核、实时调度、NUMA；只代表这台 M2 Pro 的本次会话。

## 8. 复现方式与文件

```bash
cd experiments/reproductions/002-order-book-data-structures
make -j4 all asm      # bench + 汇编
./run_all.sh          # 主实验：7 变体 × 3 次 + verify
./run_repeat.sh       # 独立复跑（A/B 对照）
./run_sweep.sh        # 32/48/64/128 层深度扫描
./build/bench --events build/events.bin --verify
```

| 文件 | 作用 |
|---|---|
| [src/case.hpp](src/case.hpp) | 事件模型、参考订单簿、七个变体、堆污染器、校验和 |
| [src/main.cpp](src/main.cpp) | 事件生成/加载、批处理计时、CSV、verify |
| [micro/search_asm.cpp](micro/search_asm.cpp) | 只用于对比 branchy/branchless 汇编 |
| [run_all.sh](run_all.sh) / [run_repeat.sh](run_repeat.sh) / [run_sweep.sh](run_sweep.sh) | 主实验 / 复跑 / 深度扫描 |
| [results/summary.md](results/summary.md) | A 组 3 次中位数 |
| [results/repeat_summary.md](results/repeat_summary.md) | B 组独立复跑 |
| [results/sweep_summary.md](results/sweep_summary.md) | 深度扫描 |
| [results/verify.txt](results/verify.txt) | 七个结构最终状态一致（PASS） |
| [results/search_asm.s](results/search_asm.s) | branchy/branchless 的机器码 |

## 9. 待验证问题

1. **更大的 N**：把 N 扩到 256/512，观察反转 vector 的长尾是否出现；但那时线性搜索会输，需要单独说明“反转”和“线性”是两个不同的优化目标。
2. **真正的逐操作尾延迟**：需要更高分辨率计时（例如可访问的 PMCCNTR 或 Linux `rdtsc`），否则 41.67 ns 的 tick 会把小操作量化掉。
3. **分配器对照**：用 jemalloc / mimalloc / 自定义 pool 重跑 `map-polluted`，区分“节点结构”与“分配器”各自的贡献。
4. **x86 对照**：同一份代码在 Linux x86 上跑，验证 CMOV 版本是否真的降低 branch mispredict，以及反转 vector 的长尾是否出现。
5. **原则 #5**：补一个 `[[likely]]` / cold code / lambda vs `std::function` 的微基准，覆盖文章最后一部分。

## 10. 参考

- 微信文章：<https://mp.weixin.qq.com/s/mYbS0pfrtIvy7slvd4eoAQ>（核实日期 2026-11-05）
- Optiver 工程文章：<https://optiver.com/working-at-optiver/career-hub/designing-low-latency-cpp-systems/>
- 相关知识点：[cache-hierarchy-mesi](../../../knowledge/arch/cache-hierarchy-mesi/README.md)、[branch-predictor](../../../knowledge/arch/branch-predictor/README.md)、[false-sharing](../../../knowledge/low-latency/false-sharing/README.md)、[timing-counters](../../../knowledge/arch/timing-counters/README.md)
