# 智能指针与所有权（Smart Pointers & Ownership）

> 模块: cpp ｜ 状态: outline ｜ 深度: L1 ｜ 创建: 2026-09-02 ｜ 更新: 2026-09-02
> 依据: HFT 题库 Q10、JD「现代 C++」（[research/03](../../research/03-tech-stack-map.md)）

## 1. 一句话定位

智能指针用 RAII 把裸指针的资源生命周期交给类型系统管理；`unique_ptr` 表达独占所有权，`shared_ptr` 表达共享所有权（引用计数）。

## 2. 为什么面试官爱问

`unique_ptr` vs `shared_ptr` 的选型、控制块结构、线程安全边界、`enable_shared_from_this`、`weak_ptr` 破环——每一步都能追问到底层实现，且与低延迟成本分析（原子计数开销）天然衔接。

## 3. 前置知识

- [move-semantics/](../move-semantics/README.md)（`unique_ptr` 只能移动不能拷贝）
- RAII（规划中：raii-exception-safety/）

## 4. 核心讲解

TODO（展开时按以下骨架填充）：
1. `unique_ptr`：零开销、删除器类型、`make_unique` 的异常安全意义。
2. `shared_ptr`：控制块（强/弱计数）、两次原子操作的成本、`make_shared` 一次分配。
3. `weak_ptr`：破环、`lock()` 的竞态意义。
4. 线程安全边界：控制块原子 ≠ 所指对象线程安全。
5. 低延迟视角：热路径避免 shared_ptr 拷贝、自定义 allocator。

## 5. Example 说明

TODO（计划实验：shared_ptr 拷贝的原子开销基准 vs unique_ptr 传递；weak_ptr 破环演示）

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| 何时用 unique vs shared？ | 默认 unique（90% 场景）；仅所有权真正共享时用 shared，其拷贝有原子计数开销 |
| shared_ptr 线程安全吗？ | 控制块计数是原子的（多线程持有/释放安全）；所指对象与同一实例的并发读写不安全 |
| 循环引用怎么破？ | 一侧改 weak_ptr；weak 不增加强计数，访问时 lock() 提升 |
| make_shared 优点与缺点？ | 一次分配、异常安全；缺点：对象与控制块同块内存，weak 长存会延迟对象内存释放 |

## 7. 深度追问记录（落盘区）

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|
| — | 尚无追问 | — | — |

## 8. 参考

- cppreference: [unique_ptr](https://en.cppreference.com/w/cpp/memory/unique_ptr) / [shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- 《Effective Modern C++》Item 18-22
