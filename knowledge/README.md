# knowledge/ — 知识点总索引

> 每个知识点 = **详细讲解（README.md）+ 可运行 example（example.cpp）+ 追问沉淀（§深度追问记录 / followups.md）**。
> 模块规划与考点依据见各模块 README 与 [research/03-tech-stack-map.md](../research/03-tech-stack-map.md)。

## 模块一览

| 模块 | 定位 | 知识点数（已展开/规划） |
|---|---|---|
| [cpp](cpp/README.md) | C++ 语言与现代特性 | 1 / 11 |
| [low-latency](low-latency/README.md) | 低延迟系统编程 | 1 / 9 |
| [network](network/README.md) | 计算机网络 | 0 / 7 |
| [os](os/README.md) | 操作系统 | 1 / 8 |
| [arch](arch/README.md) | 计算机体系结构 | 0 / 7 |
| [brainteaser](brainteaser/README.md) | 智力题与概率题 | 0 / 8 |

## 学习进度表（Agent 维护）

状态：`planned` 仅规划 ｜ `outline` 仅大纲 ｜ `expanded` 正文+example 完成 ｜ `mastered` 追问沉淀+自测通过
深度：L1 概念 ｜ L2 原理+代码 ｜ L3 生产级权衡

| 知识点 | 模块 | 状态 | 深度 | 追问数 | 最近更新 |
|---|---|---|---|---|---|
| [move-semantics 移动语义](cpp/move-semantics/README.md) | cpp | expanded | L2 | 2 | 2026-09-02 |
| [smart-pointers 智能指针](cpp/smart-pointers/README.md) | cpp | expanded | L2 | 0 | 2026-09-02 |
| [false-sharing 伪共享](low-latency/false-sharing/README.md) | low-latency | expanded | L2 | 2 | 2026-09-02 |
| [context-switch-affinity 上下文切换、调度与 CPU 亲和性](os/context-switch-affinity/README.md) | os | expanded | L2 | 2 | 2026-11-03 |

## 图例与维护说明

- 「规划中」的知识点尚未建目录，清单见各模块 README；开始学习时由 Agent 依 `AGENT.md §2.A` 创建。
- 每次追问落盘、状态变更后，Agent 会同步更新本表与模块 README。
