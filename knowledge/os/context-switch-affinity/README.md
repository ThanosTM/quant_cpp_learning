# 上下文切换、调度与 CPU 亲和性（Context Switch, Scheduler & CPU Affinity）

> 模块: os ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-03 ｜ 更新: 2026-11-03
> 依据: os 模块 P0「上下文切换、调度与 CPU 亲和性」；JD 明确要求 Linux、进程调度（[research/03](../../../research/03-tech-stack-map.md)）；低延迟轮高频题

## 1. 一句话定位

上下文切换是内核把 CPU 从一条执行流交给另一条执行流的动作：**直接成本在亚微秒到微秒级，真正昂贵的是缓存/TLB 冷启动与调度抖动**。低延迟系统的目标不是消灭切换，而是让关键线程不被意外抢占、不被迁移，并把尾延迟（p99.9）压到可预测的范围。

## 2. 为什么面试官爱问

- **区分度在“成本结构”而非名词**：能把一次切换拆成“寄存器保存 / 栈切换 / 地址空间切换 / 缓存与 TLB 污染 / 调度器记账”的人，通常真的写过或调过低延迟系统。
- **与岗位直接相关**：行情线程、策略线程、下单线程之间的每一次意外抢占或跨核迁移，都会进入交易系统的尾延迟；量化私募的 core-system 岗会追问“怎么绑核、怎么隔离、怎么测”。
- **经典追问链**：线程切换为什么比进程切换便宜 → CFS/EEVDF 怎么选下一个任务 → 绑核为什么有时反而更慢 → busy-poll 为什么比阻塞快 → 为什么 SCHED_FIFO 还会被“限流”。

## 3. 前置知识

- 进程 / 线程 / 协程基础（`process-thread-coroutine`，规划中）
- 虚拟内存与 TLB（`virtual-memory-tlb`，规划中）
- 缓存层次与一致性（`arch/cache-hierarchy-mesi`，规划中）
- 原子操作与内存序（`low-latency/memory-ordering`，规划中）

> 本文可独立阅读：涉及的前置概念会在正文就地解释；深入机制再跳转对应知识点。

## 4. 核心讲解

### 4.1 一次切换到底切换了什么

线程切换（同一进程内）与进程切换的区别，核心在**是否更换地址空间**：

```
线程 A（进程 P）                     线程 B（进程 P）
 用户栈 A ──┐                         ┌── 用户栈 B
 用户寄存器 ─┤                       ├─ 用户寄存器
             ▼                       ▼
        ┌──────────── 内核栈 A / task_struct A ────────────┐
        │  schedule() → __schedule() → context_switch()   │
        │            → switch_to(prev, next)              │
        └─────────────────────────────────────────────────┘
              │ 同一 mm：不写 CR3，TLB 基本保留
              ▼
        返回用户态执行 B

进程切换：上面的 mm 从 P1 换成 P2，还要切换页表基址（CR3）；
         现代 CPU 用 PCID/ASID 减少整 TLB 失效，但不是零成本。
```

成本可以拆成两部分：

| 成本 | 内容 | 量级（现代机器，需实测） |
|---|---|---|
| 直接成本 | 保存/恢复 callee-saved 寄存器、切换内核栈、调度器记账、返回用户态 | 亚微秒 ~ 1-2 µs |
| 地址空间成本 | 进程切换写 CR3；PCID 缓解 TLB 失效 | 额外数百 ns ~ 数 µs |
| 间接成本 | 新线程的工作集不在 L1/L2，缓存与 TLB 冷启动；分支预测器状态污染 | **常常是直接成本的数倍**，热路径越紧越明显 |
| 排队成本 | 被唤醒后不一定立刻运行，要等调度器/目标核空闲 | 几 µs ~ 数百 µs，尾延迟的主要来源 |

> 对交易系统而言，**间接成本 + 排队成本**通常比 `switch_to` 本身更值得关注：一次切换后重新预热订单簿缓存，可能比切换本身贵一个数量级。

### 4.2 一次切换的内核路径

以 x86-64 的阻塞式系统调用为例（简化）：

```
用户态 A
  │ syscall / 中断 / 异常
  ▼
entry_SYSCALL_64（保存用户上下文，切换内核栈）
  │
do_syscall_64 → sys_read / futex_wait ...
  │  条件不满足：prev->state = TASK_INTERRUPTIBLE，调用 schedule()
  ▼
__schedule()
  │ pick_next_task()：从运行队列选择 next
  ▼
context_switch(prev, next)
  │ switch_to(prev, next)：保存 prev 的 callee-saved 寄存器与栈指针
  ▼
（next 曾经也在 switch_to 里被切出，从这里“返回”）
finish_task_switch() → 返回用户态 B
```

几个容易答错的细节：

- `switch_to` 不需要保存全部寄存器：用户态通用寄存器在进入内核时已保存在内核栈/`pt_regs`；切换只需 callee-saved 部分（x86-64 SysV：`rbx`、`rbp`、`r12-r15`、`rsp` 等）。
- FP/SIMD 状态通常是**惰性切换**：新任务第一次用到 FPU 时才恢复，避免每次切换都搬 512 字节的 AVX 状态。
- 同一进程的线程共享 `mm_struct`，不换 CR3；内核线程甚至借用上一个任务的 mm（lazy TLB）。
- 自愿切换（voluntary）来自阻塞：等锁、等管道、`nanosleep`；非自愿切换（involuntary）来自时间片耗尽或被更高优先级任务抢占。**低延迟系统最怕的是后者**。

查看当前进程的切换次数：

```bash
grep -E 'voluntary|nonvoluntary' /proc/<pid>/status
# voluntary_ctxt_switches / nonvoluntary_ctxt_switches
perf stat -e context-switches,cpu-migrations,page-faults -p <pid> -- sleep 1
```

### 4.3 Linux 调度器：从 CFS 到 EEVDF，再到 RT

**普通任务（SCHED_OTHER / SCHED_NORMAL）**

- Linux 6.6 起，默认公平调度器从 CFS 切换为 **EEVDF**（Earliest Eligible Virtual Deadline First），6.12 完成相关支持（[KernelNewbies 6.6](https://kernelnewbies.org/Linux_6.6)、[KernelNewbies 6.12](https://kernelnewbies.org/Linux_6.12)）。
- EEVDF 仍按权重分配 CPU，但选择逻辑从“最左 vruntime”变成：先筛出 **lag ≥ 0（有资格）** 的任务，再选 **虚拟截止时间（VD）最早** 的任务。
- 结果是**短时间片、延迟敏感的任务可以更快被调度**；任务还可以通过 `sched_setattr()` 请求更短的时间片（`sched_runtime`）。
- 面试若问 CFS，仍要会答：vruntime、红黑树、`sched_latency`/`min_granularity`、唤醒抢占；同时补一句“现代内核默认已是 EEVDF”，是加分项。

**实时任务（SCHED_FIFO / SCHED_RR / SCHED_DEADLINE）**

- RT 优先级 1-99 永远高于普通任务；同优先级 FIFO 不轮转，RR 轮转。
- **RT throttling 是尾延迟大坑**：默认 `sched_rt_period_us=1000000`、`sched_rt_runtime_us=950000`，即 RT 任务最多占 95% CPU，超出后被限流到本周期结束，普通任务获得 50 ms/秒。一个死循环的 RT 线程会周期性“卡住”几十毫秒（[内核文档](https://docs.kernel.org/scheduler/sched-rt-group.html)）。
- `SCHED_DEADLINE` 用 runtime/deadline/period 做准入控制，适合周期性任务，但配置复杂、易受其他 RT 任务影响。
- **PREEMPT_RT 已在 Linux 6.12 合入主线**：把 spinlock 转成可睡眠的 rt-mutex、中断线程化、支持优先级继承，显著降低最坏情况延迟；代价是吞吐与复杂度（[KernelNewbies 6.12](https://kernelnewbies.org/Linux_6.12)）。
- `sched_ext`（6.12）允许用 BPF 写调度器，适合实验性策略，不适合“面试里背结论”。

### 4.4 CPU 亲和性、隔离与 NUMA

**为什么绑核**

- 避免迁移：迁移会让 L1/L2 与 TLB 冷启动，且可能跨 NUMA 节点。
- 避免和噪声邻居共享物理核：SMT 的两个超线程共享执行单元与 L1/L2。
- 让中断、内核线程、定时器尽量不落在关键核上。

**手段分层**

| 层次 | 手段 | 说明 |
|---|---|---|
| 进程级 | `taskset -c 3 ./app`、`sched_setaffinity` | 只保证“尽量”不迁移，仍可能被调度到别处 |
| 线程级 | `pthread_setaffinity_np` | 热路径线程逐个绑核；注意错误处理与 CPU 存在性 |
| 核隔离 | `isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3` | 把核从普通调度器拿走、减少 tick/RCU 干扰；`nohz_full` 需保留 housekeeping 核 |
| 资源隔离 | cpuset cgroup | 容器/多租户场景更常用；比单纯 `isolcpus` 更可控 |
| NUMA | `numactl --cpunodebind=0 --membind=0` | CPU 与内存在同一节点，避免跨 socket 访问 |
| 中断 | `/proc/irq/<irq>/smp_affinity_list`、关闭 `irqbalance` | 网卡中断不要打到关键核；必要时 RPS/RFS |
| 电源 | `cpupower frequency-set -g performance`、限制深 C-state | 深睡眠唤醒要几十微秒；C-state 是尾延迟来源 |

**一个典型的低延迟线程配方（Linux）**

```bash
# 1) 选隔离核（假设 2-3 已通过内核参数隔离）
taskset -c 3 ./strategy
chrt -f 90 ./strategy          # SCHED_FIFO，优先级 90（留 99 给更关键任务）
# 2) 进程内：mlockall(MCL_CURRENT | MCL_FUTURE)、预缺页、大页、关闭热路径分配
# 3) 网卡中断绑到非关键核；关闭 irqbalance 对该 IRQ 的干预
# 4) 测量：cyclictest -m -p 99 -i 1000 -l 10000
```

> macOS 没有 Linux 这样的硬绑核：`THREAD_AFFINITY_POLICY` 只是提示（本仓库 example 已注明）。这也是为什么量化生产环境几乎都用 Linux。

### 4.5 为什么“绑核”有时反而更慢

- **绑到 0 号核**：0 号核通常承担定时器、中断、内核线程，绑上去等于主动挤进噪声源。
- **绑到 SMT 兄弟核**：两个逻辑核共享物理核资源，另一个线程忙时，你的“独占核”并不独占。用 `/sys/devices/system/cpu/cpuN/topology/thread_siblings_list` 查兄弟。
- **只绑核、不隔离**：其他进程仍可被调度到该核；`isolcpus`/cpuset 才能把核“留出来”。
- **绑核但内存跨 NUMA**：CPU 与内存不在同一节点，延迟反而更高。
- **绑核后仍阻塞等锁**：非自愿切换不会因为绑核消失；要配合无锁队列、busy-poll、避免热路径分配。
- **过度隔离**：专用核闲置浪费吞吐；隔离核上的 RCU/定时器配置错误会引入新的抖动。

### 4.6 量化交易场景映射

| 线程 | 关键动作 | 低延迟关注点 |
|---|---|---|
| 行情接收 | 网卡 → 内核协议栈 / 内核旁路 → SPSC 队列 | IRQ 亲和性、busy-poll、避免唤醒抖动、NUMA 本地内存 |
| 策略计算 | 从队列取行情、计算信号 | 绑核、无锁、缓存友好布局、热路径零分配 |
| 下单网关 | 序列化、发送、回报处理 | 避免日志/`fsync` 进热路径、RT 优先级与限流、p99.9 测量 |

**反向思考**：如果你的策略是分钟级或日频，微秒级 OS 调优的收益远小于数据质量、回测偏差与执行成本；这些知识的主要价值是面试区分度与理解交易基础设施，不要本末倒置。

## 5. Example 说明

```bash
g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
# macOS 可用 clang++ 替换 g++；Linux 上建议再用 taskset -c 3 跑一遍对比
```

四个实验：

1. **系统调用 vs 用户态时钟**：用 `fcntl(F_GETFD)` 作为最小系统调用探针，对比 `clock_gettime()`（Linux vDSO / macOS commpage）与空循环基线。
2. **线程切换**：两个线程用管道 ping-pong，一个往返包含 2 次阻塞切换 + 4 次系统调用，测往返延迟并折算每次切换。
3. **唤醒抖动**：`nanosleep(1 ms)` 采样 500 次，输出 overshoot 的 p50/p99/max，直观看到“睡 1 ms 实际晚多久”。
4. **亲和性**：Linux 下 `pthread_setaffinity_np` 绑核并用 `sched_getcpu()` 验证；macOS 下说明只支持提示性 API。

本机实测（Apple M2 Pro，12 核，128B 缓存行；macOS Darwin 25.5；Apple clang 17；3 次运行取中位数）：

| 项目 | 结果 |
|---|---|
| 空循环基线 | ≈ 2.5 ns/call |
| `getpid()`（libc 缓存，未进内核） | ≈ 1.8 ns/call |
| `fcntl(F_GETFD)`（真实系统调用） | ≈ 132 ns/call |
| `clock_gettime()` | ≈ 21 ns/call |
| 管道 ping-pong 往返 | ≈ 4.5 µs（折合 ≈ 2.2 µs/次，含 syscall） |
| `nanosleep(1 ms)` overshoot | p50 ≈ 511 µs，p99 ≈ 567 µs，max ≈ 645 µs |

**怎么读这些数**：

- 系统调用比用户态读时钟贵约 **6-7 倍**；`getpid()` 看起来像系统调用，实际被 libc 缓存——**测成本必须用真探针**。
- 线程切换的“直接成本”在微秒级；但 ping-pong 还包含 4 次系统调用与唤醒，所以 2.2 µs/次是**上界**，不是纯 `switch_to` 成本。
- 睡眠抖动 p50 就有 ~0.5 ms，说明“定时器 + 调度 + 电源管理”才是尾延迟主角；生产上应使用 busy-poll/自旋而非 `sleep` 做精确定时。
- 平台差异很大：Linux 上这些数字会不同（通常系统调用与切换更快，抖动取决于隔离与调优）。**结论是相对关系与测量方法，不是绝对数字。**

> 方差控制：连跑 3 次取中位数；Linux 上可用 `taskset -c 3` 固定核，`cyclictest` 测系统级抖动；macOS 上关闭后台负载/接电源可减小波动。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| 线程切换和进程切换差在哪？ | 同进程线程共享地址空间，不换 CR3、TLB 基本保留；进程切换要换页表，TLB/缓存冷启动更贵 |
| 一次上下文切换多少钱？ | 直接成本亚微秒到 1-2 µs；算上缓存/TLB 污染与排队，尾延迟可到几十上百 µs，必须实测 |
| 为什么线程切换不一定比进程切换快很多？ | 直接切换路径相近；差异主要来自地址空间与 TLB，工作集大时进程切换的间接成本更高 |
| CFS 和 EEVDF 的区别？ | CFS 选最小 vruntime；EEVDF 先筛 lag≥0 的 eligible 任务，再选虚拟截止时间最早者；6.6 起默认 EEVDF |
| SCHED_FIFO 为什么还会被卡住？ | RT throttling 默认只给 RT 95% CPU，超限后被限流到周期结束，普通任务获得 50 ms/秒 |
| 绑核为什么可能变慢？ | 绑到 0 号核/中断核、SMT 兄弟核、未隔离、跨 NUMA、仍阻塞等锁，都会抵消收益 |
| busy-poll 为什么比阻塞快？ | 省掉“睡眠 → 唤醒 → 调度”的排队路径；代价是占满一个核并增加功耗 |
| 怎么测尾延迟？ | 采样本做直方图，看 p99/p99.9/max；`cyclictest`、`perf sched`、应用内 rdtsc/steady_clock 埋点 |
| 大页和 mlock 解决什么问题？ | 减少页表层级/TLB miss、避免缺页与换页；`mlock` 只锁内存，不预缺页，需配合 `MAP_POPULATE`/预触 |
| PREEMPT_RT 一定更好吗？ | 降低最坏延迟，但吞吐下降、生态复杂；低延迟交易常用“隔离核 + busy-poll + 用户态协议栈”而非全内核 RT |

## 7. 深度追问记录（落盘区）

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|
| 2026-11-03 | 为什么 `getpid()` 只有 1-2 ns，比 `clock_gettime()` 还快？ | macOS libc 缓存了 PID，`getpid()` 未真正进内核；测系统调用成本要用 `fcntl(F_GETFD)` 这类真探针 | 是（§4.1/§5 已注明） |
| 2026-11-03 | 为什么 `nanosleep(1 ms)` 在 macOS 上实际约 1.5 ms？ | 本机观察到约 0.5 ms 的固定 overshoot，可能与 macOS 定时器合并/timer slack 有关；具体机制待查证 [未核实]，Linux 上需用 `cyclictest` 单独测 | 是（§5 已注明平台差异） |

## 8. 参考

- [KernelNewbies: Linux 6.6](https://kernelnewbies.org/Linux_6.6)（EEVDF 取代 CFS，核实 2026-11-03）
- [KernelNewbies: Linux 6.12](https://kernelnewbies.org/Linux_6.12)（PREEMPT_RT 合入主线、sched_ext、EEVDF 完成，核实 2026-11-03）
- [内核文档: EEVDF Scheduler](https://docs.kernel.org/scheduler/sched-eevdf.html)（lag / virtual deadline / `sched_setattr`，核实 2026-11-03）
- [内核文档: Real-Time group scheduling](https://docs.kernel.org/scheduler/sched-rt-group.html)（RT throttling 默认 950000/1000000，核实 2026-11-03）
- [内核文档: NO_HZ](https://docs.kernel.org/timers/no_hz.html)（`nohz_full` 与抖动，核实 2026-11-03）
- [man: sched_setaffinity(2)](https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html) / [pthread_setaffinity_np(3)](https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html)（核实 2026-11-03）
- Ulrich Drepper, [What Every Programmer Should Know About Memory](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf)（缓存/TLB 成本经典参考）
- 关联知识点：[false-sharing 伪共享](../../low-latency/false-sharing/README.md)、[os 模块](../README.md)、[research/03 考点地图](../../../research/03-tech-stack-map.md)
