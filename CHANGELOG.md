# 更新日志

格式参照 Keep a Changelog，版本号主观递增；日期格式 `YYYY-MM-DD`。

## \[0.15.0] - 2026-11-05

### 新增

- 展开 `knowledge/arch/simd`（planned → expanded，L2），arch 主线第 4 站：

  - 正文：SIMD 的 lane/吞吐与延迟、SSE/AVX/AVX-512/NEON/SVE 谱系与 x86 等价写法、编译器 loop/SLP 向量化器的「合法性 + 收益」两道判断、FP 归约为何只向量化乘法、失效清单（依赖链/跨步/别名/控制流/AoS）、对齐与尾部与 SoA、浮点语义与误差预算、ILP×SIMD 叠加与带宽衰减、量化场景映射与误区。
  - `example.cpp`：三实验 + 一组汇编探针。逐元素乘法三版本（禁向量化 / 自动 / 手写 NEON，`memcmp` 逐位一致）、点积归约五版本（标量 / 自动 / 纯标量 4 链 / NEON 单累加器 / NEON 4 累加器）、48 KiB→192 MiB 工作集扫描；`used noinline` 探针覆盖别名、循环携带依赖、跨步访问、循环内条件。含 NEON 与 SSE2/AVX2 条件编译分支（x86 分支未在本机验证）。
  - 已在 Apple M2 Pro / macOS 26.5.1（Darwin 25.5.0，arm64，Apple clang 17）上用 `g++ -std=c++20 -O2 -Wall` 编译运行通过、无警告；`-O3 -march=native` 版本同样跑通。每个配置程序内 5 次取中位数、整体 3 次再取中位数。
  - 实测写入 §5：实验一 `mul_scalar/auto/simd` = 0.3185 / 0.1185 / 0.1192 ns/元素（2.69x / 2.67x，结果逐位一致）；实验二 `dot_scalar/auto/simd1/accum4/simd4` = 1.1730 / 0.8687 / 0.2906 / 0.2205 / 0.0887 ns/元素（1.00x / 1.35x / 4.04x / 5.32x / 13.22x），checksum 263492.406 → 263449（Δ −43.4062，相对约 1.6e-4）；实验三 auto 倍率 5.22x（48 KiB）→ 2.34x（192 MiB）。
  - 汇编核对：`mul_auto` 生成 `ldp q` + `fmul.4s`（4 路展开），`dot_auto` 乘法向量化但归约仍是 16 条 `fadd s`；`probe_prefix`/`probe_stride` 零向量指令，`probe_branch` 被 if-conversion 成掩码。最初 `dot_accum4` 被 SLP 合成 `fmla.4s`，加 `KEEP_FP` 内联屏障后确认纯标量 4 链。
  - `-O2` vs `-O3 -march=native`：2.69x vs 2.68x、13.22x vs 13.68x，差异在噪声范围内——本机 `-O2` 已向量化，NEON 是基线 ISA。
  - `followups.md`：5 个学习问题闭环（写入 §7），另留 2 条 x86 分支与 `-fassociative-math` 的待查证项（缺实验环境）。

### 更新

- `knowledge/arch/README.md`：SIMD 状态 planned → expanded；主线第 4 站标记已展开并写入实际实验锚点；新增「第 3 站 → 第 4 站 ILP 与 SIMD 叠加」结论与本机编译选项实测提示。
- `knowledge/README.md`：arch 模块进度 4/7 → 5/7；学习进度表新增 simd（expanded，L2，追问 5 条）。
- 根 `README.md`：arch 已展开计数 4 → 5。
- `knowledge/arch/pipeline-superscalar/README.md`：正文与参考中「simd（规划中）」改为「已展开」，补注两条并行度的关系。

## \[0.14.0] - 2026-11-05

### 新增

- 新增复现 `experiments/reproductions/002-order-book-data-structures/`，对应 Optiver / CppCon 2024「When Nanoseconds Matter」与微信文章《Optiver 低延迟交易系统设计 10 原则（上）》：
  - 重建单侧买盘订单簿：48 层、200 万条头部主导事件（30% 改最优价、30% 顶部加量、10% 盘口内、5% 深部、25% 顶部删除），事件 12B，固定深度，最终 `{count,sum,mix}` 校验和。
  - 实现 7 个变体：`std::map` 干净堆 / 污染堆、升序 vector（最优价在尾）、降序 vector（最优价在头）、手写 branchy 二分、branchless 二分、从尾部线性搜索。
  - 主实验 A 组 3 次中位数：map-clean p50 18 ns，map-polluted 55 ns（p99 129 ns），vector-front 18 ns，vector-back 17 ns，binary-branchy 15 ns，binary-branchless 15 ns，linear 13 ns。
  - 独立 B 组：map-clean 17 ns，map-polluted 59 ns，vector-front/back 18 ns，binary-branchy 14 ns，branchless 15 ns，linear 13 ns。
  - 深度扫描（32/48/64/128 层）：linear 12/13/16/25 ns vs binary 15/15/16/16 ns，交叉点在 48~64 层。
  - 结论：`std::map` 在污染堆下退化约 3 倍、长尾显著，线性搜索在小 N 下胜出——这两条复现；反转 vector 消除长尾、无分支二分降低 Bad Speculation/提升 IPC 两条未复现。
  - `micro/search_asm.cpp` + `results/search_asm.s`：证明 Clang 已把 branchy 二分 if-convert 成 `csel`，branchless 与 branchy 的机器码逐条相同；macOS 无 PMU，原文 IPC 数字无法本机验证。
  - `--verify`：7 个结构最终 `count/sum/mix` 完全一致（PASS）。
  - `run_all.sh` / `run_repeat.sh` / `run_sweep.sh`：主实验、独立复跑、深度扫描各 3 次运行并落盘。

### 更新

- `experiments/common/summarize.py` 支持 `--keys` / `--metrics` / `--checksum-cols`，001 的默认行为保持不变，002 使用自定义列聚合。
- `experiments/README.md`、`experiments/reproductions/README.md`、根 `README.md`、`knowledge/low-latency/README.md`、`CHANGELOG.md` 同步更新实验计数与索引。

## \[0.13.0] - 2026-11-05

### 新增

- 新建 `experiments/` 实验库，分两条主线：
  - `reproductions/`：复现网络低延迟技术贴/演讲，按本机硬件重测并解释偏差；
  - `agent-cases/`：由 Agent 按指令构造盲基线 + 验收指标 + 分级提示 + 参考解的性能优化案例。
- `experiments/common/bench.hpp`：纳秒计时、p50/p90/p99、QoS、ARM64/x86 缓存行操作（`prfm`、`dc civac`、`dc cvac`、`clflushopt`、`clwb`）、`cpu_relax`。
- `experiments/common/summarize.py`：把 `raw.csv` 的多配置多次运行聚合为 Markdown 中位数表。
- `templates/experiment.md`、`templates/agent-case.md`：实验报告与 Agent 案例任务书模板。
- 第一个复现 `experiments/reproductions/001-latency-slippage-cache-optimization/`：
  - 按微信文章 + CppCon 2026「The Latency Slippage」重建 64 期权重算场景：16 384 标的、每事件 140 条 128B 行、工作集约 379 MB。
  - 实现串行基线、批处理（D 扫描）、软件预取（T0/stream）、事件行驱逐、双核 48+16（完整回传/解耦/`dc cvac`）五个版本，共用同一计算函数。
  - 本机第一组 3 次运行中位数：p50 1416 → 916 ns（-35.3%），p90 1666 → 1125 ns（-32.5%），mean 1442.0 → 960.2 ns（-33.4%）；独立第二组复跑：p50 1416 → 958 ns（-32.3%），mean 1433.5 → 985.6 ns。两组稳定复现前三步（批处理+预取+事件行驱逐，约 -32%）；第四步只有 4c 的均值改善 4%~5%，4a/4b 方向不稳定。
  - 偏差实测：M2 上 D=64 没有 LFB 悬崖（反而最优）、`stream` 没有 NTA 式暴涨、双核仅 -4.4%（原文 -20%）；跨核 128B 脏行 handoff 中位数 96 ns。
  - `--stage verify`：7 个版本的 `hits/qsum/checksum` 完全一致（PASS）。
  - `micro/probe.cpp`：缓存层次延迟、`dc civac/cvac` 成本（约 1.44 ns/条）、跨核 handoff 探针。
  - `run_all.sh`：14 个配置 × 3 次运行 + 汇总 + verify + 3 次环境探针，结果落盘 `results/`。
  - `run_repeat.sh` 与独立第二组复跑数据 `results/repeat_raw.csv` / `results/repeat_summary.md`：用于暴露第三/四步的调度噪声，报告中按 A/B 两组呈现。

### 更新

- 根 `README.md`：目录树新增 `experiments/`，使用说明新增复现/案例指令，新增「实验库」进度表。
- `AGENT.md`：定位改为四支柱；新增 §2.G 实验工作流（复现 / Agent 案例）；目录命名规范新增 experiments；索引义务新增实验索引；质量门槛新增“真实编译、3 次中位数、偏差/负收益分析”。
- `experiments/README.md`、`experiments/reproductions/README.md`、`experiments/agent-cases/README.md`：实验清单、复现规范、Agent 案例契约与反作弊规则。

## \[0.12.0] - 2026-11-04

### 新增

- 展开 `knowledge/arch/pipeline-superscalar`（planned → expanded，L2）：

  - 正文：五级流水线与三类冒险、超标量/乱序/寄存器重命名/ROB、循环携带依赖与 ILP、多累加器拆归约、编译器重排限制、IPC/CPI 模型、量化场景与误区。
  - `example.cpp`：两实验（1/2/4/8 累加器浮点归约、1 条 vs 8 条纯 ALU 链），已在 Apple M2 Pro / macOS 26.5.1（Darwin 25.5.0，arm64，Apple clang 17）上用 `g++ -std=c++20 -O2 -Wall` 编译运行通过，无警告，每个配置 5 次取中位数、整体 3 次取中位数。
  - 实测写入 §5：sum1/2/4/8 = 7.28/3.70/1.97/1.14 ms（0.910/0.462/0.246/0.142 ns/元素，约 1.0x/2.0x/3.7x/6.4x）；纯 ALU chain1 91.21 ms（0.912 ns/add）vs chain8 11.35 ms（0.113 ns/add，约 8.0x）。
  - 汇编核对：`KEEP_FP` 内联屏障阻止 SLP，sum4/sum8 均为标量 `fadd d`，没有 `fadd.2d`；chain1/chain8 分别为 1 条/8 条独立 `fadd`。

### 更新

- `knowledge/arch/README.md`：pipeline-superscalar 状态 planned → expanded；学习主线第 3 站标记已完成。
- `knowledge/README.md`：arch 模块进度 3/7 → 4/7；学习进度表新增 pipeline-superscalar。
- 根 `README.md`：arch 已展开计数更新为 4。
- `knowledge/arch/pipeline-superscalar/followups.md`：4 个学习问题全部闭环，暂无未解决项。

## \[0.11.0] - 2026-11-04

### 新增

- 展开 `knowledge/cpp/modern-cpp`（planned → expanded，L2），按版本拆成五篇：

  - `README.md`：版本主线（14 补语法 → 17 补库 → 20 换范式 → 23 补实用 → 26 上前沿）、14/17/20/23/26 学习顺序、跨版本能力对照、本机标准开关与量化/HFT 使用原则。
  - `cpp14.md`：泛型 lambda、返回类型推导、放宽 constexpr、变量模板、`make_unique`、`index_sequence`、初始化捕获与小特性合集。
  - `cpp17.md`：结构化绑定、`if constexpr`、折叠表达式、inline 变量、CTAD、`optional/variant/any/string_view`、`filesystem`、`pmr`、硬件干扰尺寸（本机实测 64）、并行算法与保证复制消除。
  - `cpp20.md`：concepts、ranges、coroutines、modules 四大支柱，外加 `<=>`、`span`、`jthread`、`latch/barrier/semaphore`、`atomic_ref`、`format`、`source_location`、`bit_cast` 与 `[[likely]]`。
  - `cpp23.md`：`expected`、`print`、`flat_map/flat_set`、`mdspan`、`generator`、deducing this、`if consteval`、`unreachable`、`[[assume]]`、多维下标、`to_underlying`/`byteswap`、`move_only_function`、optional 单子、`ranges::to`/`zip`/`fold_left`。
  - `cpp26.md`：反射（P2996）、契约（P2900）、`#embed`、包索引、占位变量、`simd`、`linalg`、`submdspan`、`inplace_vector`、`hive`、`<rcu>`/hazard pointer、`std::execution` 与 adoption 建议。
  - `example14.cpp`（C++14）：泛型 lambda、初始化捕获、变量模板、放宽 constexpr、`make_unique`、`index_sequence`、二进制/chrono 字面量、`exchange`、`shared_lock`，已用 `clang++ -std=c++14 -O2 -Wall` 编译运行通过。
  - `example17.cpp`（C++17）：结构化绑定、`if constexpr`、折叠表达式、`optional`/`variant`/`any`、`string_view`、`filesystem`、`pmr`、inline 变量、CTAD、硬件干扰尺寸，已用 `clang++ -std=c++17 -O2 -Wall` 编译运行通过。
  - `example.cpp`（C++20）：14/17/20 三段式巡礼，已用 `clang++ -std=c++20 -O2 -Wall` 编译运行通过，无警告。
  - `example23.cpp`（C++23）：`expected` 单子链、`flat_map`、`mdspan`、`ranges::to`、`optional::transform`、`resize_and_overwrite`、`views::zip`、`println`，已用 `clang++ -std=c++23 -O2 -Wall` 编译运行通过。
  - 实测输出：`example14.cpp` 打印 `generic=42 pi=3.14159 constexpr=5050 unique=7 tuple=6 capture=42 bin=170 timeout=100 exchange=1`；`example17.cpp` 打印 `has_size=yes binding=1 2.5 three optional=7 variant=variant any=3.14 view=view`、`ctad=2.5 clamp=10 gcd=6 lcm=12 byte=66 path="/tmp/probe.txt" fold=10 inline=1 interference=64`；`example.cpp` 打印 `generic=42 pi=3.1416 constexpr=5050 unique=7 tuple=6 capture=42`、`binding: 1 2.5 three`、`optional=7 view=view variant=variant fold=10 ctad=2.5 inline=1`、`concept=42 ranges=20 span=5 <=>=true atomic=1 bitcast=0x3f800000`；`example23.cpp` 打印 `expected=43 flat_map=3 mdspan=6 to=3 optional=9 text=xbc zip=66`。
  - 本机缺口实测：C++20 的 `std::midpoint`、`std::osyncstream` 缺失；C++23 的 `generator`/`move_only_function`/`stacktrace`/`enumerate`/`chunk`/`join_with`/`cartesian_product`/`start_lifetime_as` 缺失；C++26 模式下仅 pack indexing、`#embed`（Clang 扩展）、`= delete("reason")`、占位变量可用（`[[assume]]` 为 C++23 特性，在 C++26 模式下同样可用），反射/契约/`simd`/`linalg`/`inplace_vector`/`hive` 不可用。

### 更新

- `knowledge/cpp/README.md`：`modern-cpp-23-26/`（planned）→ `modern-cpp/`（expanded），补充五篇版本子文档导航。
- `knowledge/README.md`：cpp 模块进度 3/11 → 4/11；学习进度表新增 modern-cpp（expanded，L2，2026-11-04）。
- `research/03-tech-stack-map.md`：P2 考点「C++23/26 新特性速览」更新为「现代 C++ 版本演进（14/17/20/23/26）」。
- 根 `README.md`：cpp 已展开计数更新为 4。

## \[0.10.0] - 2026-11-04

### 新增

- 展开 `knowledge/arch/branch-predictor`（planned → expanded，L2）：

  - 正文：前端预测方向/目标、BTB/RAS、静态预测、1-bit/2-bit、相关/TAGE、预测失败惩罚、CSEL/CMOV 无分支、`[[likely]]` 的真实作用、控制变量与汇编核对、量化场景与误区。
  - `example.cpp`：三实验（有序 vs 无序、随机数据 branch vs branchless、有序/交替/随机模式敏感性），已在 Apple M2 Pro / macOS 26.5.1（Darwin 25.5.0，arm64，Apple clang 17）上用 `g++ -std=c++20 -O2 -Wall` 编译运行通过，无警告，每个配置 5 次取中位数、整体 3 次取中位数。
  - 实测写入 §5：sorted 12.97 ms（0.405 ns/元素）、random 97.85 ms（3.058 ns/元素，7.55x）、random branchless 16.07 ms（0.502 ns/元素，6.11x 于 random branch）、alternating 19.10 ms（0.597 ns/元素，约 5x 快于 random）；按 50% 误预测粗估单次约 5.3 ns。
  - 汇编核对：`sum_branch` 生成 `tbz` 真分支，`sum_branchless` 生成 `and/asr` + `add` 无分支。

### 更新

- `knowledge/arch/README.md`：branch-predictor 状态 planned → expanded；学习主线第 2 站标记已完成。
- `knowledge/README.md`：arch 模块进度 2/7 → 3/7；学习进度表新增 branch-predictor。
- 根 `README.md`：arch 已展开计数更新为 3。
- `knowledge/arch/branch-predictor/followups.md`：4 个学习问题全部闭环，暂无未解决项。

## \[0.9.1] - 2026-11-04

### 文档

- 深化 `knowledge/arch/cache-hierarchy-mesi` §4.1/§4.3：
  - 新增 §4.1.1「Cache 的物理结构」：地址 `tag/set index/offset` 切分、`sets = capacity/(line × ways)`、128KB/128B/8-way 算术示例、直接映射/全相联/组相联对比、VIPT/PIPT、line vs sector、替换策略。
  - 新增 §4.3.1「MESI 状态迁移」：本地读写命中/ miss 表、snoop（BusRd/BusRdX）反应表、驱逐规则；四条关键路径（E→M 静默升级、S→M BusUpgr、M→S 供数、写 miss RFO）。
  - 新增 §4.3.2「嗅探 vs 目录」：snooping/directory、MOESI/MESIF、coherence 与 consistency 的区别。
  - 修正 §4.4 标题「关联系」→「相联度」。
  - §7 新增第 5 条追问记录；`knowledge/README.md` 追问数 4 → 5、更新日期改为 2026-11-04。

## \[0.9.0] - 2026-11-03

### 新增

- 展开 `knowledge/arch/cache-hierarchy-mesi`（planned → expanded，L2）：

  - 正文：L1/L2/SLC/DRAM 容量-延迟台阶（含本机 `hw.perflevel0` 参数）、行粒度与写分配、时间/空间局部性与矩阵遍历、MESI 四态与真/伪共享、64B sector 实测边界、相联度/替换/写策略、量化场景映射与误区。
  - `example.cpp`：三实验（随机指针追逐的工作集曲线、2048×2048 矩阵行/列遍历、MESI 偏移扫描），已在 Apple M2 Pro / macOS 26.5.1（Darwin 25.5.0，arm64，Apple clang 17）上用 `g++ -std=c++20 -O2 -Wall` 编译运行通过，无警告，3 次运行取中位数。
  - 实测写入 §5：L1（≤128 KB）约 1.2 ns/hop，256 KB 起 3.7-6.5 ns，16 MB 约 23 ns，128 MB 约 111 ns；矩阵行优先 1.49 ms vs 列优先 15.26 ms（约 10.2x）；MESI 同 64B sector 约 5.2-5.4 ns/op，跨 64B 降至 1.15 ns/op，跨 128B 1.04 ns/op。
  - 关键发现：`sysctl hw.cachelinesize=128`，但写-写干扰在 **64B 边界**消失；正文按「128B 行、64B 一致性 sector」解释，并给出保守 padding 建议。

### 更新

- `knowledge/arch/README.md`：cache-hierarchy-mesi 状态 planned → expanded；学习主线第 1 站标记已完成，实验锚点改为已实现的三实验。
- `knowledge/README.md`：arch 模块进度 1/7 → 2/7；学习进度表新增 cache-hierarchy-mesi。
- 根 `README.md`：arch 已展开计数更新为 2。
- `knowledge/arch/cache-hierarchy-mesi/followups.md`：4 个学习问题全部闭环，暂无未解决项。

## \[0.8.1] - 2026-11-03

### 文档

- `knowledge/network/README.md`：新增「网络主线（建议学习顺序）」与「本机实验环境注意」：

  - 把知识点排成 1→8 的推进顺序，逐站给出「这一站解决什么」与 example 实验锚点；Nagle 归入 2 站，避免 `socket-parameters` 只剩背结论。
  - 明确真题落位：富途握手/队列 → 1 站，垒昂 epoll/`TCP_NODELAY` → 3/5 站，宽德粘包拆包 → 4 站；真题落 `interview/system/` 并双向互链。
  - 记录 Apple M2 Pro / macOS 的实验限制：无 epoll、`splice` 仅 Linux、`tc netem` 不在本机、队列观测需 Linux、抓包需 sudo；Linux-only 分支需在容器内编译验证。
- `knowledge/network/README.md` 知识点清单：补 `tcp-framing`（P1，依据宽德真题与 [research/03](research/03-tech-stack-map.md)），各条依据列补 JD/面经锚点。
- `knowledge/README.md`：network 模块规划数 0/7 → 0/8。

## \[0.8.0] - 2026-11-03

### 新增

- 展开 `knowledge/arch/timing-counters`（planned → expanded，L2）：

  - 正文：TSC/`rdtsc` 的序列化（`lfence`/`rdtscp`）与 invariant TSC 频率换算、多核偏移；ARM `CNTVCT_EL0` 与 Apple 24 MHz 计数器；`clock_gettime`/`steady_clock`/`mach_absolute_time` 的单位、分辨率与开销；单次包围 vs 批次计时的方法论；`perf stat`/Instruments 的定位；量化交易场景与误区清单。
  - `example.cpp`：四实验（平台时钟源与换算、时钟读取吞吐、分辨率差值分布、批次放大），已在 Apple M2 Pro / macOS 26.5.1（Darwin 25.5.0，arm64，Apple clang 17）上用 `g++ -std=c++20 -O2 -Wall` 编译运行通过，无警告，跑 3 次取中位数。
  - 实测写入 §5：`mach_timebase=125/3`（41.667 ns/tick，24 MHz）；读取成本 `mach_absolute_time` 5.07 ns、`steady_clock` 14.99 ns、`CLOCK_MONOTONIC` 19.67 ns、`MONOTONIC_RAW` 16.51 ns、`UPTIME_RAW` 14.66 ns、内联 `CNTVCT_EL0` 低于空循环基线；分辨率 `CLOCK_MONOTONIC` 1000 ns，其余 tick 类时钟 41/42 ns；批次 B=1 16.45 ns/条 → B=1000 0.31 ns/条。

### 更新

- `knowledge/arch/README.md`：timing-counters 状态 planned → expanded；学习主线第 0 站标记已完成。
- `knowledge/README.md`：arch 模块进度 0/7 → 1/7；学习进度表新增 timing-counters。
- 根 `README.md`：arch 已展开计数更新为 1。
- `knowledge/arch/timing-counters/followups.md`：4 个学习问题全部闭环，暂无未解决项。

## \[0.7.0] - 2026-11-03

### 文档

- `knowledge/arch/README.md`：新增「体系结构主线（建议学习顺序）」与「本机实验环境注意」：
  - 把 7 个 planned 知识点排成 0→6 的学习顺序，给出每站要解决的面试现象与 example 实验锚点；`timing-counters` 虽为 P2，但作为测量地基提到第 0 站。
  - 标注与已展开知识点的联读关系（false-sharing → MESI、cache ↔ virtual-memory-tlb、cpu-topology ↔ context-switch-affinity）。
  - 明确每站验收标准（README 八节 + 编译运行通过的 example + 本机实测数字 + 追问/自测）。
  - 记录 Apple M2 Pro 实验环境的四个坑（128B 缓存行、无 AVX、无 perf/rdtsc、P/E 核噪声）。
  - 未新增知识点，arch 模块进度仍为 0/7。

## \[0.6.0] - 2026-11-03

### 新增

- 展开 `knowledge/cpp/object-model`（planned → expanded，L2）：

  - 正文：对象内存布局（对齐、EBO、尾部 padding 复用、standard-layout）、vptr/vtable（Itanium ABI 的 `-1`/`-2` 槽、析构器槽、去虚化）、多重继承（this 调整、thunk、offset-to-top、对象切片）、虚继承、RTTI 与 `dynamic_cast` 成本、量化场景权衡。

  - `example.cpp`：五实验（sizeof/EBO、vptr 与 vtable 槽、多重继承指针调整、虚继承共享子对象、RTTI），已在 Apple M2 Pro / macOS Darwin 25.5（arm64，Apple clang 17）上用 `g++ -std=c++20 -O2 -Wall` 编译运行通过，无警告。

  - 实测结果写入 §5：`sizeof(Multi)=32`（尾部 padding 复用）、Base2 偏移 +16 / offset-to-top -16、`sizeof(Diamond)=48` 等。

### 更新

- `knowledge/README.md`：cpp 模块 1/11 → 3/11（顺带修正原有过期计数），学习进度表新增 object-model。
- `knowledge/cpp/README.md`：object-model 状态 planned → expanded。
- 根 `README.md`：cpp 已展开计数更新为 3。
- `interview/cpp/001`：与 object-model 建立双向互链。

## \[0.5.0] - 2026-11-03

### 新增

- 展开 `knowledge/os/virtual-memory-tlb`（planned → expanded，L2）：

  - 正文：多级页表与地址翻译、TLB 覆盖范围/PCID/TLB shootdown、minor/major/非法缺页、预触/`mlock`/大页/池化、量化交易场景映射与常见误区。
  - `example.cpp`：两实验（64 MB 匿名内存首次触页 vs 已驻留、64 KB→128 MB 随机指针追逐），已在 Apple M2 Pro / macOS Darwin 25.5（16 KB 页）上用 `g++ -std=c++20 -O2 -Wall`（Apple clang 17）编译运行验证。
  - 实测结果写入 §5：首次触页约 1.08 µs/页、已驻留约 0.22 µs/页、差值约 0.90 µs/页；随机指针追逐 1.2/4.9/7.1/64/103 ns（64 KB→128 MB）。

### 更新

- `knowledge/README.md`：os 模块进度 1/8 → 2/8；学习进度表新增 virtual-memory-tlb。
- 根 `README.md`：os 模块已展开计数更新为 2。
- `knowledge/os/README.md`：虚拟内存知识点状态改为 expanded，学习路径第 2 站标记已展开。

## \[0.4.0] - 2026-11-03

### 新增

- 展开 `knowledge/os/context-switch-affinity`（planned → expanded，L2）：

  - 正文：切换的成本结构（直接/地址空间/缓存-TLB/排队）、内核切换路径、EEVDF 与 RT 调度、RT throttling、PREEMPT_RT、CPU 亲和性与隔离配方、绑核反模式、量化交易线程映射。
  - `example.cpp`：四实验（系统调用 vs vDSO/commpage、管道 ping-pong 切换、`nanosleep` 唤醒抖动、Linux 绑核），已在 Apple M2 Pro / macOS Darwin 25.5 上用 `g++ -std=c++20 -O2 -Wall`（Apple clang 17）编译运行验证；Linux 亲和性分支为条件编译，未在本机执行。
  - 实测结果写入 §5：系统调用约 132 ns、读时钟约 21 ns、切换往返约 4.5 µs、1 ms 睡眠的 overshoot p50 约 511 µs。

- `knowledge/os/README.md` 新增规划知识点：`interrupt-irq-affinity`（中断/NAPI/IRQ 亲和性）、`kernel-preemption-rt`（抢占/PREEMPT_RT/C-state 抖动），并新增「低延迟主线」学习顺序表。

### 更新

- `knowledge/README.md`：os 模块进度 0/6 → 1/8；学习进度表新增 context-switch-affinity。
- 根 `README.md`：os 模块已展开计数更新为 1。

## \[0.3.1] - 2026-09-02

### 新增

- `knowledge/cpp/smart-pointers` 追问落盘 4 条（README §7）：删除器零开销与 EBO 机制、手写 `MySharedPtr`（控制块双原子计数 / CAS 提升 / 集体弱份额）、`unique_ptr<T[]>` 用法与 UB 坑、`enable_shared_from_this` 三坑与异步自共享范式。

  - 配套实验文件（均已通过 `g++ -std=c++20 -O2 -Wall` 编译运行验证）：`example-ebo-deleter.cpp`、`example-my-shared-ptr.cpp`、`example-unique-array.cpp`、`example-shared-from-this.cpp`。

### 更新

- `knowledge/cpp/smart-pointers/README.md`：§4.2（EBO 与 `[[no_unique_address]]`、数组特化 UB 警示）、§4.5（自共享三坑与 `owner_before` 鉴别）正文随追问扩写；§5 收录追问衍生实验清单。

## \[0.3.0] - 2026-09-02

### 新增

- `research/05-company-recruit-scan.md`：八家量化私募（幻方、天演、佳期、世纪前沿、衍复、宽德、九坤、明汯）岗位结构、校招专项、招聘流程、薪资口径与笔面试真题扫描，49 条来源按 P0-P3 分级，含横向对比（3.1-3.4）、共性趋势（7 条）、备考启示（7 条）、待拆解真题清单（编程/概率/工程/网络四类）与信息局限说明。

### 更新

- `research/03-tech-stack-map.md`：新增编译链接（P1）、TCP 粘包（P1）、IPC 全家（P1）、回归统计（P1）考点；赌徒破产/几何概率/手写代码/订单簿等条目依据补强（八家真题实据）。

- `research/README.md`：文档清单收录 05 号调研。

## \[0.2.0] - 2026-09-02

### 新增

- 展开 `knowledge/cpp/smart-pointers`：outline → expanded（L2）。

  - 正文：所有权模型选型、`unique_ptr` 零开销与 move-only、`shared_ptr` 控制块与原子计数、`weak_ptr` 破环、线程安全三层边界与 `enable_shared_from_this`、低延迟热路径视角（联动 false-sharing）。

  - `example.cpp`：四实验（sizeof 零开销 / 分配次数 2 vs 1 / 循环引用泄漏与破环 / 多线程计数原子性），已通过 `g++ -std=c++20 -O2 -Wall` 编译运行验证。

- `interview/experiences/001`：九坤「智能指针线程安全性」考点并入 smart-pointers §4.5，待拆解清单对应项勾销。

## \[0.1.0] - 2026-09-02

### 新增

- 仓库框架初始化：README 导航、AGENT 维护规范、写作模板、`scripts/new_topic.sh` 脚手架脚本。

- `research/`：量化私募 C++ 岗 JD 分析、核心系统岗 JD 分析、技术栈考点地图、笔试面试题型全景（含来源链接，核实日期 2026-09-02）。

- `knowledge/`：六大模块（cpp / low-latency / network / os / arch / brainteaser）索引与考点规划。

  - seed 完整知识点：`cpp/move-semantics`、`low-latency/false-sharing`（正文 + 可运行 example + 追问记录示例）。

  - seed 大纲知识点：`cpp/smart-pointers`。

- `interview/`：真题索引；seed 真题 `cpp/001`（构造/析构函数中调用虚函数）；面经要点摘录 `experiences/001`。

