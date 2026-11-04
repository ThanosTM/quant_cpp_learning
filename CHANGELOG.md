# 更新日志

格式参照 Keep a Changelog，版本号主观递增；日期格式 `YYYY-MM-DD`。

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

