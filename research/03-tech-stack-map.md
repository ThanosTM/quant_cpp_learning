# 技术栈考点地图（知识点规划总纲）

> 最后核实日期：2026-09-02
> 优先级：P0 = 高频必考，优先展开；P1 = 常考，次优先；P2 = 加分项，有余力再展开。
> 依据：[01-jd-quant-cpp.md](01-jd-quant-cpp.md)、[02-jd-core-system.md](02-jd-core-system.md)、[04-interview-landscape.md](04-interview-landscape.md)、[05-company-recruit-scan.md](05-company-recruit-scan.md) 所引面经与题库。

## 1. cpp — C++ 语言与新特性

| 考点 | 优先级 | 依据 |
|---|---|---|
| 移动语义 / 完美转发 / 值类别 | P0 | 九坤面经（右值引用、移动语义、完美转发）；HFT 题库高频 |
| 智能指针与所有权（unique/shared/weak、控制块） | P0 | HFT 题库；JD「现代 C++」 |
| 对象模型：vtable/vptr、多重继承内存布局、RTTI | P0 | 九坤面经；富途真题（虚表指针、构造函数调虚函数） |
| STL 容器底层：map vs unordered_map、vector 扩容、迭代器失效 | P0 | 富途真题（map/unordered_map 底层） |
| RAII 与异常安全 | P0 | HFT 题库 |
| const 正确性 / 关键字细节（static、inline、explicit…） | P1 | 九坤面经（const 各场景） |
| 模板进阶：SFINAE → concepts、CRTP、类型萃取 | P1 | HFT 题库（senior 轮） |
| constexpr 编译期计算 | P1 | HFT 题库 |
| lambda 捕获与闭包 | P1 | HFT 题库 |
| 分配器 / 内存池 / placement new | P1 | HFT 题库（Q20 memory pool） |
| 现代 C++ 版本演进（14/17/20/23/26；协程、expected、反射等） | P2 | JD「新特性」开放性要求 |

## 2. low-latency — 低延迟系统编程

| 考点 | 优先级 | 依据 |
|---|---|---|
| 内存序与原子操作（relaxed/acquire/release/seq_cst） | P0 | HFT 题库 Q1 |
| 伪共享与缓存行对齐（alignas / interference size） | P0 | HFT 题库 Q2；JD「缓存行对齐」 |
| 无锁结构：SPSC 环形队列、CAS、ABA | P0 | HFT 题库 Q4-Q6；多家低延迟轮真题 |
| 缓存友好数据布局（SoA vs AoS、热/冷字段分离） | P0 | HFT 题库 Q15 |
| 分支预测与 [[unlikely]]、内联权衡 | P1 | HFT 题库 Q16-17；JD「分支预测」 |
| 内存池与热路径分配规避 | P1 | HFT 题库 Q20 |
| 延迟测量：rdtsc、直方图、尾延迟 | P1 | phantomcodeai 低延迟原则 |
| 内核旁路与网卡调优（onload/dpdk、busy-poll） | P2 | 核心系统岗 JD |
| NUMA 感知 | P2 | phantomcodeai 低延迟原则 |

## 3. network — 计算机网络

| 考点 | 优先级 | 依据 |
|---|---|---|
| TCP 三次握手/四次挥手、半连接/全连接队列 | P0 | 富途真题（三次握手及底层、半连接全连接） |
| TCP 可靠性机制：重传、滑动窗口、拥塞控制 | P0 | JD「网络原理」 |
| I/O 多路复用：select/poll/epoll 原理与水平/边缘触发 | P0 | JD「epoll」 |
| TCP vs UDP 及行情组播场景 | P1 | 行情系统方向 |
| TCP 粘包与拆包处理 | P1 | 宽德 2026 实习二面真题（§2.6） |
| 零拷贝：sendfile/splice/mmap | P1 | 核心系统岗 |
| RDMA / 内核旁路网络 | P2 | 低延迟岗加分 |
| TLS/HTTPS 与协议设计（RPC、序列化） | P2 | 垒昂面经（RPC 通信） |

## 4. os — 操作系统 / arch — 计算机体系结构

| 考点（os） | 优先级 | 考点（arch） | 优先级 |
|---|---|---|---|
| 虚拟内存、页表与 TLB | P0 | 缓存层次与 MESI 一致性 | P0 |
| 上下文切换、调度与 CPU 亲和性 | P0 | 分支预测器原理 | P0 |
| 系统调用成本与 vDSO | P1 | 流水线/乱序执行/超标量 | P1 |
| epoll 内核实现侧 | P1 | SIMD 向量化 | P1 |
| 缺页中断与大页 | P1 | NUMA 拓扑 | P1 |
| 信号与文件 I/O（page cache） | P2 | 硬件预取、频率与 C-state | P2 |

依据：JD 明确点名「epoll、进程调度、内存管理；缓存行对齐、SIMD、分支预测」（[CSDN 文库](https://wenku.csdn.net/answer/7iaq1z1grz6y)）；HFT 题库性能与体系结构部分。

## 5. brainteaser — 智力题 / 概率题（+ 待补领域模块）

| 考点 | 优先级 | 依据 |
|---|---|---|
| 期望值与博弈类问题 | P0 | phantomcodeai Round 7 题型 |
| 条件概率 / 贝叶斯 | P0 | 同上 |
| 组合计数（隔板法、鸽笼） | P0 | 同上 |
| 赌徒破产 / 随机游走 | P1 | 同上；天演 2023 笔试真题（1 vs 2 元本金、胜率 2/3 变式） |
| 几何分布 / 均匀和 / 次序统计量；几何概率（木棍三段） | P1 | 同上；天演真题（木棍裁三段成三角 + 变式） |
| 经典谜题（100 囚犯、信封悖论等） | P1 | 同上 |
| 回归的几何与统计性质（最小二乘、均匀撒点） | P1 | 衍复 2023 笔试第一题（§2.5） |
| 心算训练（两位数乘法、基点换算） | P0 技能 | 同上，accuracy first |
| 【待补】交易/行情领域知识：订单簿、撮合、订单类型、逆向选择 | P1 | phantomcodeai Round 6；02 号调研 §3；九坤手写订单簿面试真题（§2.7） |
| 【待补】手写代码题：atoi、随机数生成、LeetCode 经典（如 200 岛屿）、LRU、线程池+并发安全队列、Orderbook | P0 | 富途真题、稳博笔试；世纪前沿/九坤手撕 LRU、宽德线程池 + 并发安全队列、九坤 Orderbook（05 号调研 §6.3） |

> 手写代码题落位说明：算法类真题归 `interview/algorithm/`，语言实现类（atoi、智能指针手写）归 `interview/cpp/`；本表仅作考点追踪。
