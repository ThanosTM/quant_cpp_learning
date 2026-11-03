# 虚拟内存：页表、TLB 与缺页中断（Virtual Memory, Page Tables, TLB & Page Faults）

> 模块: os ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-03 ｜ 更新: 2026-11-03
> 依据: os 模块 P0「虚拟内存：页表 / TLB / 缺页中断」；JD「内存管理」；HFT 题库常问缺页与大页

## 1. 一句话定位

虚拟内存让每个进程拥有独立的虚拟地址空间；CPU 通过页表把虚拟地址翻译成物理地址，TLB 缓存翻译结果，缺页中断负责按需建立映射或从磁盘调页。**低延迟视角下，缺页和 TLB miss 都是可测量的微秒级事件：热路径必须预触页、锁页、尽量用大页，避免“第一次访问”发生在行情高峰。**

## 2. 为什么面试官爱问

- **量级题**：一次缺页多少纳秒/微秒？TLB miss 与缺页有什么区别？大页为什么能降低 TLB miss？能把机制和数字同时说清的人不多。
- **工程题**：`malloc` 返回的内存能立刻用吗？`mlock` 能避免缺页吗？`fork` 为什么在低延迟系统里是坏味道？这些问题直接对应交易系统的启动预热与热路径纪律。
- **岗位关联**：行情/订单簿需要大块稳定内存，订单与行情缓冲不能在半途扩容；低延迟岗会追问“你怎么保证稳态零缺页”。

## 3. 前置知识

- 进程 / 线程与地址空间（`process-thread-coroutine`，规划中）
- 缓存层次与一致性（`arch/cache-hierarchy-mesi`，规划中）
- 上下文切换与 CPU 亲和性（[context-switch-affinity](../context-switch-affinity/README.md)，已展开）

## 4. 核心讲解

### 4.1 虚拟地址如何翻译成物理地址

每个进程有一张（或多级）页表，页表项（PTE）记录“虚拟页 → 物理页框”的映射及权限。以 x86-64、4 KB 页为例：

```
48 位虚拟地址（4 级页表，每级 512 项）：
 47      39 38      30 29      21 20      12 11          0
┌──────────┬──────────┬──────────┬──────────┬─────────────┐
│ PML4 idx │ PDPT idx │  PD idx  │  PT idx  │  page offset │
│  (9 bit) │  (9 bit) │  (9 bit) │  (9 bit) │   (12 bit)   │
└──────────┴──────────┴──────────┴──────────┴─────────────┘
     │          │          │          │
     ▼          ▼          ▼          ▼
   PML4   →   PDPT   →    PD    →    PT    →  物理页框 + offset
```

- 每一级表项 8 字节、512 项，正好覆盖 4 KB 的一页；一次 page walk 最多读 4 次内存。
- PTE 里除了物理页框号，还有 present、writable、user、accessed、dirty、NX 等标志；权限不符或 present=0 就会触发缺页异常（#PF）。
- 现代 x86 还有可选的 5 级页表（57 位虚拟地址）；是否需要背？记住“多级 + 每级索引 + 页内偏移”即可。
- **Apple Silicon / macOS 的基础页是 16 KB**（本机 `sysconf(_SC_PAGESIZE)` 实测 16384），AArch64 的页表层级和索引位数与 x86 的 4 KB 方案不同；Linux x86 常见 4 KB。**页大小不同，缺页次数、TLB 覆盖范围和内部碎片都会变。**

> 面试追问：“为什么页表要分级？”——为了节省空间：稀疏地址空间只需要分配实际用到的子表，而不是一整张巨大的线性表。

### 4.2 TLB：地址翻译的缓存

TLB（Translation Lookaside Buffer）缓存最近的“虚拟页号 → 物理页框 + 权限”翻译结果：

- TLB 命中：MMU 直接用缓存结果，不访问页表；
- TLB miss：硬件 page walker 走页表（x86），或由软件填充（部分架构）；page-walk 本身还要读内存，可能再 miss 缓存；
- TLB 通常分 L1 ITLB / DTLB 与 L2 统一 TLB，具体项数随微架构变化，**不要硬背型号参数，记住“容量有限、按页覆盖”**。

覆盖范围可以用算术感受一下：

```
64 项 × 4 KB  = 256 KB       （很小的热数据就可能超过）
2048 项 × 4 KB = 8 MB
64 项 × 2 MB  = 128 MB       （同样项数，覆盖范围放大 512 倍）
```

这就是大页的核心收益：**同样数量的 TLB 项覆盖更大的工作集，减少 page walk**。

TLB 还有两个低延迟系统必须知道的点：

1. **TLB shootdown**：当内核修改某个页的映射（`munmap`、`mprotect`、`mremap`、`fork` 的 COW、页迁移等），其他核上缓存的旧翻译必须失效，内核会发 IPI（处理器间中断）同步。映射范围大、核数多时，这是一次全局同步，**热路径上应避免动态改映射**。
2. **PCID / ASID**：上下文切换时不必把整个 TLB 冲掉，而是给不同地址空间打标签；这降低了进程切换的 TLB 成本，但不等于零成本（见 [context-switch-affinity](../context-switch-affinity/README.md)）。

### 4.3 缺页中断：minor、major 与非法访问

“TLB miss”和“缺页”是两件事：

| 事件 | 含义 | 处理者 | 典型成本 |
|---|---|---|---|
| TLB miss | 翻译不在 TLB，但页表里有合法映射 | 硬件 page walker / 软件 | 几十 ns ~ 数百 ns |
| minor page fault | 页已在内存，但 PTE 没有映射（如首次访问匿名页、COW） | 内核缺页处理 | ~1 µs 量级（含清零） |
| major page fault | 页不在内存，需要磁盘 I/O（swap、文件未缓存） | 内核 + I/O | 几十 µs ~ 毫秒 |
| 非法访问 | 没有权限 / 地址未映射 | 内核发 SIGSEGV | 进程崩溃 |

一次匿名页首次访问的简化路径：

```
用户访存 → MMU 查 PTE：present=0
  → #PF 陷入内核
  → do_page_fault() → 找到 vma → handle_mm_fault()
  → 分配物理页、清零、填 PTE
  → 返回用户态，重新执行那条访存指令
```

几个常见来源：

- **匿名页首次写**：`malloc`/`mmap` 只是预留虚拟地址，第一次写才分配物理页并清零；
- **COW（copy-on-write）**：`fork` 后父子共享只读页，任何一方写才复制；`fork` 本身不复制物理内存，但会产生大量后续缺页；
- **文件映射**：`mmap` 一个文件后首次读，若 page cache 没有就从磁盘读（major）；写回也可能触发 I/O；
- **swap**：被换出的页再次访问是 major fault；
- **栈增长 / guard page**：栈越界先触发缺页，内核扩展栈；超过限制则 SIGSEGV。

测量方式：

```bash
awk '{print "minor="$10, "major="$12}' /proc/<pid>/stat   # 字段 10/12：minor/major faults
perf stat -e page-faults -p <pid> -- sleep 1
/usr/bin/time -v ./app                                    # 运行结束的 fault 统计
```

### 4.4 低延迟手段：预触、锁页、大页、池化

**启动阶段就把“未来的缺页”花掉**：

1. **预触页（prefault）**：分配后按页写一遍（或读一遍），强制建立 PTE。Linux 可用 `mmap(..., MAP_POPULATE)` 让内核在映射时预缺页；macOS 没有等价的通用标志，显式 touch 更可控。
2. **锁页（lock）**：`mlock` / `mlockall(MCL_CURRENT | MCL_FUTURE)` 防止页被换出。注意语义：
   - `mlock` / `mlockall(MCL_CURRENT)` 成功返回时，已映射页保证驻留；
   - `MCL_FUTURE` 让未来映射也进入锁定范围，但未来页仍可能在首次访问时才建立映射；
   - `MCL_ONFAULT` 明确表示“缺页时才锁”，不会预触；
   - 因此**不能只调用 `mlockall` 就假设所有未来分配都零缺页**，启动时仍要显式预触，并检查 `RLIMIT_MEMLOCK`（`ulimit -l`）。
3. **大页（huge page）**：
   - Linux THP（Transparent Huge Pages）：2 MB 页自动/半自动合并，`madvise(MADV_HUGEPAGE)` 提示；一次 2 MB 区域只缺页一次（相比 4 KB 页减少 512 倍缺页次数），且 TLB 覆盖范围更大（[内核文档](https://docs.kernel.org/admin-guide/mm/transhuge.html)）；
   - 显式 hugetlb：`MAP_HUGETLB` / `vm.nr_hugepages` 预留 2 MB 或 1 GB 页，延迟更可控但需要预留、易碎片化（[内核文档](https://docs.kernel.org/admin-guide/mm/hugetlbpage.html)）；
   - macOS 没有 Linux THP 那套机制，基础页本身就是 16 KB。
4. **内存池 / 预留容量**：所有热路径对象（订单簿、行情缓冲、SPSC 队列）在启动时一次分配；`std::vector` 用 `reserve()` 固定容量，禁止在热路径 `push_back` 触发扩容。
5. **避免 `fork` / `mmap` / `munmap` / `mprotect`**：这些操作会复制页表或触发 TLB shootdown；交易进程应在启动时完成地址空间布局，之后不再改映射。
6. **NUMA 本地化**：匿名页在首次触达的 NUMA 节点分配；跨节点访问延迟更高（后续 `low-latency/numa-awareness` 展开）。
7. **稳态验收**：预热后跑一段空转/回放，确认 `minflt`、`majflt` 增量为 0；把“交易时段出现 major fault”当成事故告警。

**反向思考**：大页不是无条件更快——它会增加内部碎片、分配延迟和内存占用，THP 的合并/压缩还可能引入卡顿；`mlockall` 过量会撞上 `RLIMIT_MEMLOCK` 导致后续分配失败。**先测量，再决定用哪一层手段。**

### 4.5 量化交易场景映射

| 场景 | 虚拟内存风险 | 做法 |
|---|---|---|
| 行情缓冲 / 订单簿 | 启动后还在扩容、首次触页 | 启动时按最大容量分配，预触 + 锁页，必要时大页 |
| SPSC 队列 | 队列内存跨页、首次写入才分配 | 分配后整段预触；队列容量固定 |
| 策略回放/热身 | 回放数据时才缺页 | 启动时用历史数据预热，确保热路径页已驻留 |
| 日志/落盘 | `mmap` 文件首次访问 major fault | 热路径避免文件映射；日志走异步线程/预分配 |
| 进程模型 | `fork` 后 COW 缺页 | 预先创建线程/进程，避免在交易时段 fork |

**反向思考**：分钟级/日频策略的瓶颈通常在数据质量、回测偏差与执行成本，而不是缺页；这些知识主要用于低延迟岗位面试与理解交易基础设施，不要为了“看起来专业”在无关场景过度调优。

### 4.6 常见误区

- **“`malloc` 返回就能用”**：错。它通常只返回虚拟地址，首次写才分配物理页。
- **“`mlock` 会帮我预触所有页”**：不准确。`mlock` 锁定的是已映射页；未来映射/`MCL_ONFAULT` 仍可能首次访问才缺页。
- **“TLB miss 就是缺页”**：不是。TLB miss 是翻译缓存未命中，页表可能合法；缺页是映射不存在或权限不符。
- **“页越大越好”**：大页降低 TLB 压力和缺页次数，但会增加内部碎片、分配延迟与内存占用。
- **“虚拟内存就是 swap”**：虚拟内存是地址空间抽象；swap 只是后备存储的一种。
- **“进程切换才刷 TLB”**：改映射（`munmap`/`mprotect`/`mremap`）也会触发 TLB shootdown。

## 5. Example 说明

```bash
g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
# macOS 可用 clang++ 替换 g++；Linux 上可用 taskset -c 3 固定核后复测
```

两个实验：

1. **缺页成本**：`mmap` 64 MB 匿名内存，先按页写一遍（首次触页，触发 minor fault），再按页读一遍（已驻留），用 `getrusage` 统计 minor/major fault，比较两次的每页成本。
2. **工作集 / TLB 覆盖**：构造随机指针链，测量 64 KB → 128 MB 工作集下的依赖加载延迟；工作集逐级超过 L1/L2/L3 与 TLB 覆盖范围，延迟阶梯上升。

本机实测（Apple M2 Pro，macOS Darwin 25.5，页大小 16 KB；5 次运行取中位数）：

| 项目 | 结果 |
|---|---|
| 页大小 / 缓冲区 / 页数 | 16 KB / 64 MB / 4096 页 |
| 首次触页 | ≈ 1.08 µs/页（总 ≈ 4.4 ms） |
| 已驻留再次访问 | ≈ 0.22 µs/页 |
| 差值（缺页 + 清零） | ≈ 0.90 µs/页 |
| `getrusage` | minor faults +4096，major faults +0 |

指针追逐（随机依赖加载，5 次中位数）：

| 工作集 | 延迟 |
|---|---|
| 64 KB | ≈ 1.2 ns/步 |
| 512 KB | ≈ 4.9 ns/步 |
| 4 MB | ≈ 7.1 ns/步 |
| 32 MB | ≈ 64 ns/步 |
| 128 MB | ≈ 103 ns/步 |

**怎么读这些数**：

- 首次触页约 1 µs/页，4096 页就是约 4.4 ms 的启动成本；如果这发生在交易时段，就是一次明显的卡顿。
- 工作集从 4 MB 到 32 MB，延迟从 7 ns 跳到 64 ns，说明已经越过 L3/TLB 的舒适区；128 MB 时每次随机访问约 100 ns，接近 DRAM 延迟 + page walk。
- 指针追逐同时包含缓存 miss 与 TLB miss，不能单独把 64 ns 全部归因于 TLB；要单独测 TLB，需要构造“数据在缓存但翻译超出 TLB”的访问模式（进阶实验）。
- 首次触页实验方差较大（受零页填充、后台负载影响），所以用 5 次中位数；Linux 4 KB 页的每页缺页成本会不同，但“缺页是微秒级、启动预触可消除”这个结论不变。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| 页表为什么分级？ | 节省空间，稀疏地址空间只分配用到的子表；x86-64 4 级、每级 512 项、48 位 VA |
| TLB 是什么？ | 缓存虚拟页到物理页框的翻译；命中不用 page walk，未命中要走页表 |
| TLB miss 和缺页的区别？ | TLB miss 只是翻译没缓存，页表可能合法；缺页是 PTE 不存在/权限不符，要进内核处理 |
| minor fault 与 major fault？ | minor 页已在内存（首次匿名页、COW），major 需要磁盘 I/O；后者可能是毫秒级 |
| 一次缺页多少开销？ | minor 约 1 µs 量级（含清零），取决于页大小；major 几十 µs 到毫秒，必须避免 |
| 大页为什么快？ | 同样 TLB 项覆盖更大内存，减少 page walk；一次 2 MB 缺页替代 512 次 4 KB 缺页 |
| `mlock` 能避免缺页吗？ | 能锁住已映射页；未来映射/`MCL_ONFAULT` 仍可能首次访问才缺页，需显式预触 |
| `malloc` 的内存能直接用吗？ | 不能假设；通常是懒分配，首次写触发缺页 |
| `fork` 为什么在低延迟系统里危险？ | 复制页表 + COW，父子后续写都会缺页；应在启动阶段完成进程/线程创建 |
| 怎么保证稳态零缺页？ | 启动预触 + 锁页 + 固定容量 + 不回改映射；用 `minflt/majflt` 或 `perf` 验证 |
| 4 KB 页与 16 KB 页怎么选？ | 页越大 TLB 覆盖越大、页表越小，但内部碎片更大；通常由平台决定，Linux 可用大页 |

## 7. 深度追问记录（落盘区）

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|
| 2026-11-03 | 为什么本机页大小是 16 KB，而不是常见的 4 KB？ | Apple Silicon/macOS 采用 16 KB 基础页（本机 `sysconf` 实测 16384）；更大基础页降低页表层级/TLB 压力，但内部碎片更大。具体架构取舍原因 [未核实]；Linux x86 常见 4 KB，可用 2 MB/1 GB 大页 | 是（§4.1/§5 已注明） |
| 2026-11-03 | `mlock` 之后首次访问还会缺页吗？ | 对已映射页，`mlock`/`mlockall(MCL_CURRENT)` 成功返回即保证驻留；但 `MCL_FUTURE` 的未来映射仍可能在首次访问时才建立，`MCL_ONFAULT` 明确不预触。启动时应显式预触并检查 `RLIMIT_MEMLOCK` | 是（§4.4 已细化） |

## 8. 参考

- Ulrich Drepper, [What Every Programmer Should Know About Memory](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf)（页表、TLB 与缓存成本经典参考）
- [内核文档: Transparent Hugepage Support](https://docs.kernel.org/admin-guide/mm/transhuge.html)（THP 减少缺页次数与 TLB miss，核实 2026-11-03）
- [内核文档: HugeTLB Pages](https://docs.kernel.org/admin-guide/mm/hugetlbpage.html)（2 MB/1 GB 大页与 `/proc/meminfo`，核实 2026-11-03）
- [man: mmap(2)](https://man7.org/linux/man-pages/man2/mmap.2.html)、[mlock(2)](https://man7.org/linux/man-pages/man2/mlock.2.html)、[getrusage(2)](https://man7.org/linux/man-pages/man2/getrusage.2.html)（核实 2026-11-03）
- 关联知识点：[context-switch-affinity](../context-switch-affinity/README.md)、[os 模块](../README.md)、[research/03 考点地图](../../../research/03-tech-stack-map.md)
