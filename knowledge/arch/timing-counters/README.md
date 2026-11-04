# 计时与计数器：rdtsc / clock_gettime / 性能计数器

> 模块: arch ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-03 ｜ 更新: 2026-11-03
> 依据: JD 低延迟/高性能要求；arch 主线第 0 站（测量地基）；与 [low-latency/latency-measurement](../../low-latency/README.md)（规划中）、[os/syscall-vdso](../../os/README.md)（规划中）交叉。

## 1. 一句话定位

计时是性能实验的地基：**选错时钟源，5 ns 的差异会被 1 µs 的量化噪声直接淹没**；`rdtsc` 是周期计数器而不是墙上时钟，必须理解序列化、频率换算与跨核偏移，才能用它做可信的延迟测量。

## 2. 为什么面试官爱问

- 低延迟岗位的区分度题：能把 `rdtsc` / `clock_gettime` / `steady_clock` 的**单位、分辨率、开销、适用场景**说清楚的人很少，多数人只会调 API。
- 现象题极多：「为什么你的微基准测出 0 ns 或 41.7 ns？」「为什么加一条 `lfence` 结果就变了？」「多核上为什么两个核的 TSC 对不上？」——答案都在时钟源与测量方法本身。
- 与 [`low-latency/latency-measurement`](../../low-latency/README.md)（规划中，rdtsc/直方图/尾延迟）、[`os/syscall-vdso`](../../os/README.md)（规划中，`clock_gettime` 为什么不进内核）直接互链，是后续所有性能实验的前置。

## 3. 前置知识

- C++ `<chrono>` 的基本用法（`steady_clock` / `duration_cast`）；无专门知识点，参考 [cppreference chrono](https://en.cppreference.com/w/cpp/chrono)。
- 用户态/内核态与系统调用概念（[os/syscall-vdso](../../os/README.md)，规划中）。
- 调度、频率与 CPU 亲和性对测量噪声的影响（[os/context-switch-affinity](../../os/context-switch-affinity/README.md)，已展开）。
- 本站是 [cache-hierarchy-mesi](../cache-hierarchy-mesi/README.md)（规划中）的前置：后续缓存实验全部依赖这里的计时基线。

## 4. 核心讲解

### 4.1 先分清四件事：时钟源、接口、单位、测量方法

| 层次 | 例子 | 单位 | 分辨率（本机实测 / 典型） | 典型用途 |
|---|---|---|---|---|
| 硬件计数器 | x86 TSC / ARM `CNTVCT_EL0` | tick / cycle | ARM 41.7 ns（24 MHz）；x86 约 0.3-0.5 ns/tick | 最低开销的相对计时 |
| OS 抽象 | `mach_absolute_time`、`clock_gettime` | tick 或 ns | `mach` 41.7 ns；`clock_gettime` 41-1000 ns（取决于 clock id，见 §5） | 跨进程、可移植计时 |
| C++ 抽象 | `std::chrono::steady_clock` | ns（名义） | 41/42 ns | 业务代码、跨平台 |
| 性能计数器 | PMU（`perf stat`、Instruments） | 事件计数 | 取决于事件 | 解释「为什么慢」，不是计时 |

关键区分：

- **分辨率（resolution）**：两次读能分辨的最小差值；`steady_clock::period` 写成 `1/1e9` 不代表实际能分辨 1 ns。
- **精度（accuracy）**：读数与真实时间的偏差；受频率漂移、NTP 调整影响。
- **开销（overhead）**：读一次时钟本身的成本；它决定你能否测一个 5 ns 的操作。

### 4.2 x86：TSC 是周期计数器，不是墙上时钟

`RDTSC` 返回 64 位时间戳计数器（Time Stamp Counter）。现代 CPU 通常是 **invariant TSC**：频率固定，不随 P/C-state 或核心频率变化，因此可以换算成纳秒。

两个必须知道的坑：

1. **`rdtsc` 不是序列化指令**。乱序执行可能把它提前到前面的工作之前，导致测出的区间偏短。测量区间要用 `lfence` 或 `rdtscp` 包起来：

```cpp
#include <x86intrin.h>

inline uint64_t tsc_begin() {
    _mm_lfence();          // 等前面的指令全部完成
    return __rdtsc();
}

inline uint64_t tsc_end() {
    unsigned aux = 0;
    uint64_t t = __rdtscp(&aux);  // 等前面的指令完成，aux 是 TSC_AUX（常含核号）
    _mm_lfence();          // 防止后续指令被提前
    return t;
}
```

2. **TSC 频率不等于核心频率**。i9 标称 5 GHz，TSC 可能只有 2-3 GHz；直接用 `delta / 5e9` 算秒会错一倍。换算方式：
   - CPUID leaf `0x15` / `0x16` 给出 TSC 频率（部分型号）；
   - 用已知时长的 `steady_clock` 校准：跑 100 ms，读 TSC 差值，得到 ticks/ns；
   - Linux `constant_tsc` / `nonstop_tsc` flag 只说明频率稳定，不直接给频率。

多核场景：invariant TSC 不保证跨 socket 同步，虚机迁移/休眠也会破坏连续性。跨核测延迟前先做偏移校验；`rdtscp` 的 `aux` 可用于检测是否发生了核迁移。绝对时间（跨机器）用 PTP/NTP，不要用 TSC。

### 4.3 ARM64 与 Apple Silicon：24 MHz 的系统计数器

ARMv8 的 `CNTVCT_EL0` 是虚拟计数器，通常用户态可读（本机实测可读；某些虚拟化配置可能被 trap）；`CNTFRQ_EL0` 给出频率。Apple M 系列上 `CNTVCT_EL0` 的频率是 **24 MHz**（`CNTFRQ_EL0` 报告该值），即一个 tick = 41.667 ns：

```cpp
inline uint64_t cntvct() {
    uint64_t v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

// 换算：ns = ticks * numer / denom
mach_timebase_info_data_t tb{};
mach_timebase_info(&tb);   // Apple M2 Pro: 125 / 3 → 41.667 ns/tick
```

本机实测的三个事实：

- `CNTVCT_EL0` 与 `mach_absolute_time` 的读数差是**恒定偏移**（约 -1.118e13 ticks），说明同频率、不同起点，**不能直接互换**。
- `mach_absolute_time` 是函数调用，读一次约 **5 ns**；直接 `mrs` 的开销低到被空循环基线淹没（< 0.3 ns 量级）。
- `mach_absolute_time` **睡眠期间不递增**；需要跨休眠计时用 `mach_continuous_time`。

macOS 的 `clock_gettime` 还有一个平台独有的坑：`CLOCK_MONOTONIC` 实测分辨率是 **1000 ns**，而 `CLOCK_MONOTONIC_RAW` / `CLOCK_UPTIME_RAW` 才是 41/42 ns。测纳秒级延迟时不要用 `CLOCK_MONOTONIC`。

### 4.4 测量方法论：单次包围 vs 批次计时

如果被测操作是 5 ns，而时钟读一次要 15 ns，那么「读时钟 → 操作 → 读时钟」测到的是噪声，不是操作。正确做法是**批次计时**：

```
T_op ≈ (T_end - T_start - 空循环开销) / N
```

要求 `T_end - T_start` 远大于时钟分辨率（经验值：至少 1000 个 tick），否则量化误差会主导结果。本机实测（见 §5）：

| 每个样本包住的 add 条数 B | 每次样本 | 折算单条 |
|---|---|---|
| 1 | 16.45 ns | 16.45 ns |
| 10 | 16.4 ns | 1.64 ns |
| 100 | 37.5 ns | 0.37 ns |
| 1000 | 310 ns | **0.31 ns** |

B=1000 时，15 ns 的时钟开销被摊成 0.015 ns/条，才看得到真实的 0.31 ns/条（约 1 cycle）。

其他必须遵守的规则：

- **warmup**：先跑 1e5-1e6 次，让分支预测、cache、频率进入稳态。
- **多次重复取中位数**，同时保留 p99/p999；平均值会掩盖尾延迟。
- **固定核/固定 QoS**：Linux 用 `taskset` 绑核；macOS 没有等价的显式绑核，至少用线程 QoS 影响调度，并识别 P/E 核差异。否则迁移与频率变化会放大方差。
- **防止优化掉**：结果要被使用（`asm volatile("" : "+r"(x))` 或全局 sink）；不要滥用 `volatile`，它会引入额外的内存访问，扭曲被测代码。
- **隔离噪声**：关闭日志/`printf`/系统调用；测量代码本身不要放在热路径循环里。
- **报告分布**：微基准至少给 min / p50 / p99 / max，不只给平均值。

### 4.5 性能计数器：解释「为什么慢」，而不是「多慢」

`perf stat` 给的是**事件计数**，不是时间：

```bash
perf stat -e cycles,instructions,cache-misses,branch-misses ./prog
# IPC = instructions / cycles
# cycles / 频率 ≈ 秒（频率变化时只能用 PMU 的 cycles 做相对比较）
```

常用事件：`cycles`、`instructions`、`cache-references`、`cache-misses`、`branch-misses`、`stalled-cycles-frontend/backend`。`perf c2c` 可直接定位伪共享（见 [false-sharing](../../low-latency/false-sharing/README.md)）。macOS 没有 Linux `perf`，对应工具是 Instruments（Time Profiler / CPU Counters）；PMU 在用户态直接读取受权限限制，通常走 Instruments。

注意：PMU 计数会被多路复用、采样开销和 SMT 共享影响；不要把 `cycles` 当墙上时间，也不要在生产热路径里常开。

### 4.6 量化交易场景映射

- **相对延迟**（行情进 → 信号出）：同一核上用 TSC/`cntvct`，记录 `t0/t1`，落直方图；不要每笔都 `printf`。
- **绝对时间**（跨机器对时）：PTP/NTP + `CLOCK_REALTIME`；TSC 只做相对差值。
- **尾延迟**：按阶段分桶（网卡 → 内核 → 策略 → 下单），看 p99/p999，而不是总平均。
- **陷阱**：用 `rdtsc` 差值直接当 ns（忘换算/频率漂移）；跨核比较 TSC（偏移）；用 `CLOCK_MONOTONIC` 测 ns 级（macOS 1 µs）；微基准不 warmup、不绑核。

### 4.7 常见误区清单

- `steady_clock::period == 1 ns` 就以为能分辨 1 ns（实际受硬件 tick 限制）。
- 用两次读的差值测「读时钟要多久」——差值低于一个 tick 时只会得到 0 或 41.7 ns。
- 单次包围一个 5 ns 操作，然后困惑为什么结果是 0/41.7/1000 ns。
- 只看平均延迟，不看 p99/p999。
- 把 `perf stat` 的 `cycles` 直接当纳秒。
- 在热路径里读时钟、打日志、做字符串格式化。

## 5. Example 说明

```bash
g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
```

本机环境：Apple M2 Pro / macOS 26.5.1（Darwin 25.5.0）/ arm64 / Apple clang 17。以下为 3 次运行的中位数。

**实验一：平台时钟源**

- `mach_timebase = 125 / 3` → 1 tick = 41.667 ns（24.0 MHz）。
- `CNTVCT_EL0` 与 `mach_absolute_time` 差值恒定（约 -1.118e13 ticks）：同频率、不同起点。

**实验二：读一次时钟的成本（吞吐法，N=20,000,000）**

| 接口 | 读取成本（扣除空循环 0.30 ns） |
|---|---|
| `mach_absolute_time` | 5.07 ns |
| `CNTVCT_EL0`（内联 `mrs`） | ≈0（低于空循环基线，无法分辨） |
| `std::chrono::steady_clock` | 14.99 ns |
| `clock_gettime(CLOCK_MONOTONIC)` | 19.67 ns |
| `clock_gettime(CLOCK_MONOTONIC_RAW)` | 16.51 ns |
| `clock_gettime(CLOCK_UPTIME_RAW)` | 14.66 ns |

**实验三：分辨率（连续两次读的最小非零差值）**

| 接口 | 最小非零差值 | 差值为 0 的比例 |
|---|---|---|
| `mach_absolute_time` | 41.7 ns | 86.1% |
| `CNTVCT_EL0` | 41.7 ns | 97.8% |
| `steady_clock` | 41.0 ns | 60.9% |
| `clock_gettime(CLOCK_MONOTONIC)` | **1000 ns** | 97.9% |
| `clock_gettime(CLOCK_MONOTONIC_RAW)` | 41.0 ns | 61.7% |
| `clock_gettime(CLOCK_UPTIME_RAW)` | 41.0 ns | 62.1% |

**实验四：批次放大**

| B（每个样本的 add 条数） | 每次样本 | 折算单条 |
|---|---|---|
| 1 | 16.45 ns | 16.45 ns |
| 10 | 16.4 ns | 1.64 ns |
| 100 | 37.5 ns | 0.37 ns |
| 1000 | 310 ns | **0.31 ns** |

观察点：

- `CLOCK_MONOTONIC` 的 1 µs 量化是 macOS 特有；Linux 上它通常是 ns 级。跨平台代码优先 `steady_clock`，macOS 纳秒级可用 `MONOTONIC_RAW`/`UPTIME_RAW`。
- 单次包围的样本值会被时钟开销与量化主导；B 越大，分摊越薄，最终逼近 1 cycle（0.31 ns）。
- 本实验未绑核，数值有约 5-10% 波动；复测建议跑 3 次取中位数。
- 代码中的 `asm volatile("" ::: "memory")` 与 `"+r"(sink)` 是防止循环被优化掉的编译器屏障，不是硬件屏障。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| `rdtsc` 是时钟吗？ | 不是，是周期计数器；频率固定（invariant TSC）时才能换算成纳秒，且不等于核心频率 |
| 为什么 `rdtsc` 前后要 `lfence`/`rdtscp`？ | `rdtsc` 不序列化，乱序执行会把读提前；`lfence`/`rdtscp` 保证区间边界 |
| 多核 TSC 为什么可能对不上？ | invariant TSC 不保证跨 socket 同步，虚机迁移/休眠也会破坏；跨核测量前要校验偏移，`rdtscp` 的 aux 可查核号 |
| `clock_gettime` 为什么比系统调用快？ | Linux 走 vDSO，用户态直接读共享数据，不陷入内核；macOS 走 commpage 类似机制 |
| 为什么 macOS 的 `CLOCK_MONOTONIC` 只有 1 µs 分辨率？ | 实现基于微秒级时间源；要 ns 级用 `MONOTONIC_RAW`/`UPTIME_RAW` 或 `steady_clock` |
| 怎么测一个 5 ns 的操作？ | 批次循环 N 次测总时间再除以 N；N 要足够大，使总时长 >> 时钟分辨率；多次取中位数 |
| 为什么微基准会测出 0 ns / 41.7 ns？ | 两次读落在同一个硬件 tick，差值被量化为 0 或一个 tick；要么加批次，要么换更高分辨率时钟 |
| 平均延迟够吗？ | 不够；量化交易看 p99/p999 与直方图，平均值会掩盖尾延迟 |
| `perf stat` 的 cycles 等于时间吗？ | 不等于；cycles / 当前频率 ≈ 秒，频率变化时只能做相对比较；IPC = instructions/cycles |
| 测量本身会不会影响结果？ | 会；时钟读、`printf`、原子操作、cache 污染都算开销；要隔离、预热、批处理 |

## 7. 深度追问记录（落盘区）

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|
| 2026-11-03 | 为什么用「两次读的差值」测时钟读取开销会得到不可信的数字？ | 差值低于一个硬件 tick 时只能取 0 或 41.7 ns；且只能看到第二次读的一部分。改吞吐法：外层读两次、循环读 N 次。 | §4.4、§5 实验二 |
| 2026-11-03 | 为什么 `CNTVCT_EL0` 与 `mach_absolute_time` 的差值是一个大常数？ | 同频率（24 MHz）、不同起点（虚拟计数器偏移）；差值恒定说明频率一致，但读数不能直接互换，换算仍用 `mach_timebase`。 | §4.3、§5 实验一 |
| 2026-11-03 | 为什么 macOS 的 `CLOCK_MONOTONIC` 分辨率是 1000 ns，而 `steady_clock` 是 41 ns？ | `CLOCK_MONOTONIC` 走微秒级时间源；`steady_clock`/`MONOTONIC_RAW`/`UPTIME_RAW` 走 24 MHz 计数器。 | §4.3、§5 实验三 |
| 2026-11-03 | 为什么 B=1 时每次样本 16.5 ns，B=1000 时单条只要 0.31 ns？ | 每次样本都含一次时钟读开销（约 15 ns）；B 越大，固定开销被摊得越薄，最终逼近真实的 1 cycle/条。 | §4.4、§5 实验四 |

## 8. 参考

- Intel SDM Vol.3 Chapter 17（Time Stamp Counter）与 `RDTSC/RDTSCP` 指令说明：[Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)（核实日期 2026-11-03）
- ARM Architecture Reference Manual，`CNTVCT_EL0` / `CNTFRQ_EL0`：[ARM DDI 0487](https://developer.arm.com/documentation/ddi0487/latest/)（核实日期 2026-11-03）
- Apple `mach_absolute_time` / `mach_continuous_time`：[Apple Developer](https://developer.apple.com/documentation/kernel/1462446-mach_absolute_time)（核实日期 2026-11-03）
- macOS `clock_gettime` 支持的 clock id（`CLOCK_MONOTONIC_RAW` / `CLOCK_UPTIME_RAW`）：`man clock_gettime`（本机 macOS 26.5.1，核实日期 2026-11-03）
- C++ `<chrono>`：[cppreference](https://en.cppreference.com/w/cpp/chrono)（核实日期 2026-11-03）
- Linux `perf` 事件与 `perf stat`：[perf wiki](https://perf.wiki.kernel.org/)（核实日期 2026-11-03）
- Agner Fog 微架构手册（指令延迟/吞吐）：[agner.org](https://www.agner.org/optimize/)（核实日期 2026-11-03）
- 关联知识点：[false-sharing](../../low-latency/false-sharing/README.md)、[virtual-memory-tlb](../../os/virtual-memory-tlb/README.md)、[context-switch-affinity](../../os/context-switch-affinity/README.md)
- 关联真题：`interview/system/`（待补，低延迟计时类真题后续拆解归档）
