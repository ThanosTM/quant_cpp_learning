# 001 量化私募 C++ 岗面经摘录（第一批）

> 收录: 2026-09-02 ｜ 状态: 摘录完成，待逐题拆解
> 来源链接集中在 [research/04-interview-landscape.md](../../research/04-interview-landscape.md) §面经链接清单；本文件只做结构化摘录与拆题规划，避免面经原文失联。

## 一、整体流程印象（国内量化私募 / 核心系统开发）

1. 简历筛选（看重 C++ 项目深度、算法竞赛/刷题量、Linux 实践）；
2. 笔试（在线 OJ：算法题 2-4 道，可能加 C++ 语言细节选择题）；
3. 2-3 轮技术面（语言细节 → 系统设计 → 手写代码）；
4. HR 面 / 交叉面。
海外岗位（HFT）普遍再加一轮 online assessment 与低延迟系统深挖（见 research/04 八阶段流程）。

## 二、公司面经摘录

### 九坤（方向：量化研究/开发岗 C++ 面）
- 语言：`std::move` 与移动语义细节、完美转发、智能指针线程安全性、虚函数与对象模型。
- 系统性：多线程数据结构、无锁队列认识、内存序初步。
- 拆题规划：move 语义追问链已并入 `cpp/move-semantics/`；对象模型真题 → `interview/cpp/001`。

### 富途（方向：C++ 服务端开发）
- 经典题：构造/析构函数中调用虚函数（已拆为 [interview/cpp/001](../cpp/001-constructor-virtual-function.md)）。
- 容器：`map` 与 `unordered_map` 底层、扩容与迭代器失效。
- 手写：字符串/整数转换（atoi 类）、线程安全单例。

### 垒昂（方向：核心系统开发）
- 系统题：epoll 与 IO 多路复用、TCP 参数（`TCP_NODELAY`）、时间轮/定时器设计。
- 场景题：行情转发链路如何做到低延迟（对应 research/02 行情系统 JD）。
- 拆题规划：epoll → `knowledge/network/epoll-select-poll/`；定时器设计 → 待建 `interview/system/` 题。

### 稳博（方向：量化开发）
- 概率智力题（硬币/骰子期望类）+ 基础 C++（const/static 语义）。
- 拆题规划：概率题 → `knowledge/brainteaser/` 按题落盘。

## 三、共性与优先级结论（对照 research/03）

- 语言细节 + 手写代码是**每一场都出现**的底座（P0）；
- 多线程/内存模型、epoll、容器底层是**第二梯队**（P0-P1）；
- 概率智力题国内初面常见，海外 HFT 更重 mental math 与概率（P0）；
- 低延迟领域题（伪共享、无锁、零拷贝）在核心系统岗出现率高（P1）。

## 四、待拆解清单

- [ ] 富途：map vs unordered_map 底层 → 新建 `interview/cpp/002-stl-map-underlying.md`
- [ ] 富途：手写 atoi（含溢出/非法输入处理）→ 新建 `interview/algo/001-handwritten-atoi.md`
- [ ] 九坤：智能指针线程安全性 → 并入 `cpp/smart-pointers/` 追问区
- [ ] 垒昂：epoll LT/ET 区别与惊群 → 新建 `interview/system/001-epoll-et-lt-thundering-herd.md`
- [ ] 垒昂：定时器数据结构设计（时间轮 vs 最小堆）→ 新建 `interview/system/002-timer-design.md`
- [ ] 稳博：硬币期望类概率题 2 道 → 落入 `knowledge/brainteaser/expected-value/`

## 五、复盘笔记（随模拟面试追加）

（待补：每次模拟作答后，记录卡壳点与复述不顺的地方）
