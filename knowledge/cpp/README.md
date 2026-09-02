# cpp 模块 — C++ 语言与新特性

> 岗位 JD 的第一硬性要求（[research/01](../../research/01-jd-quant-cpp.md)）；面试中最常被追问到语言标准层面的模块。

## 知识点清单

| 知识点 | 目录 | 状态 | 优先级 | 依据 |
|---|---|---|---|---|
| 移动语义 / 完美转发 | [move-semantics/](move-semantics/README.md) | expanded | P0 | 九坤面经；HFT 题库 |
| 智能指针与所有权 | [smart-pointers/](smart-pointers/README.md) | outline | P0 | HFT 题库 Q10 |
| 对象模型：vtable/vptr、多重继承、RTTI | object-model/ | planned | P0 | 九坤、富途真题 |
| STL 容器底层（map/unordered_map/vector） | stl-containers/ | planned | P0 | 富途真题 |
| RAII 与异常安全 | raii-exception-safety/ | planned | P0 | HFT 题库 Q9/Q18 |
| const 正确性与关键字细节 | const-and-keywords/ | planned | P1 | 九坤面经 |
| 模板进阶：SFINAE → concepts、CRTP | templates-crtp/ | planned | P1 | HFT 题库 Q12/Q14 |
| constexpr 编译期计算 | constexpr/ | planned | P1 | HFT 题库 Q11 |
| lambda 捕获与闭包 | lambdas/ | planned | P1 | HFT 题库 Q13 |
| 分配器与内存池 | allocators/ | planned | P1 | HFT 题库 Q20（与 low-latency 交叉） |
| C++23/26 新特性速览 | modern-cpp-23-26/ | planned | P2 | JD「新特性」 |

优先级定义见 [research/03-tech-stack-map.md](../../research/03-tech-stack-map.md)。

## 模块说明

- 内存序与原子操作归入 [low-latency](../low-latency/README.md) 模块，避免重复维护；本模块相关真题互链。
- 面试特征：国内面试喜欢**从一行代码追问到底层**（如 `std::move(x)` 展开成什么、map 红黑树 vs hash），因此每个知识点的「深度追问记录」是本模块的核心资产。
