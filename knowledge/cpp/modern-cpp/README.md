# 现代 C++ 版本演进（C++14 → C++26）

> 模块: cpp ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-04 ｜ 更新: 2026-11-04
> 依据: [research/03](../../../research/03-tech-stack-map.md)（P2「C++23/26 新特性速览」）；[research/01](../../../research/01-jd-quant-cpp.md)（JD「熟练现代 C++ 11/14/17/20」）
> 版本篇: [cpp14](cpp14.md) ｜ [cpp17](cpp17.md) ｜ [cpp20](cpp20.md) ｜ [cpp23](cpp23.md) ｜ [cpp26](cpp26.md)

## 1. 一句话定位

现代 C++ 不是某一个孤立特性，而是 **C++14/17/20/23/26 五个版本累积出的表达力、安全性与性能工具集**。本知识点按版本拆成五篇：每篇回答三个问题——这个版本解决了上一版的什么痛点、哪些特性必须会、在量化低延迟代码里怎么用（以及什么时候不该用）。

## 2. 为什么面试官爱问

- **JD 直接点名**：量化私募 C++ 岗普遍写「熟练掌握现代 C++（11/14/17/20）」，面试会用它区分「写过 C++」和「理解标准演进」的候选人。

- **高频追问集中在版本边界**：`auto` 返回类型推导是 14、结构化绑定是 17、concepts/ranges 是 20、`std::expected` 是 23——答错版本说明知识是拼凑的。

- **低延迟场景有真实取舍**：`constexpr`/`consteval` 把计算前移、`std::span` 消除拷贝、`std::atomic_ref` 给非原子对象加原子语义、`std::expected` 在热路径替代异常、`[[likely]]`/`[[unlikely]]` 配合分支预测。这些都能从语言特性追到汇编与缓存行为。

- **新特性不等于该用**：面试官常问「C++20 的 coroutine 能用在撮合热路径吗」「modules 为什么生产不用」，考的是工程判断而非背特性列表。

## 3. 前置知识

- 移动语义与完美转发：[move-semantics](../move-semantics/README.md)（`std::move`、值类别、`noexcept`）。

- 智能指针与所有权：[smart-pointers](../smart-pointers/README.md)（`unique_ptr`/`shared_ptr` 控制块、EBO）。

- 对象模型：[object-model](../object-model/README.md)（vtable/vptr、布局、RTTI）——理解「零开销抽象」的底座。

- 模板基础（函数模板、类模板、特化）：[templates-crtp](../templates-crtp/README.md) 规划中；本主题的 C++14/17 篇会先补最小必要模板语法。

- RAII 与异常安全：[raii-exception-safety](../raii-exception-safety/README.md) 规划中；读 C++23 `std::expected` 前建议先理解异常的成本模型。

> 学习顺序建议严格按 **14 → 17 → 20 → 23 → 26**。C++14/17 是「补齐现代 C++ 基本功」，C++20 是「范式升级」，C++23/26 是「按需选学」；跳读 20 直接看 23 会在 concepts/ranges 上卡住。

## 4. 核心讲解

### 4.1 版本主线：每个版本只做一件事

| 版本 | 发布 | 一句话主题 | 代表特性 | 量化落点 |
|---|---|---|---|---|
| C++14 | 2014 | **把 C++11 补完整** | 泛型 lambda、放宽 constexpr、`make_unique`、变量模板 | 编译期常量、回调、所有权 |
| C++17 | 2017 | **工程化基础设施** | 结构化绑定、`if constexpr`、`optional/variant/string_view`、`filesystem`、`pmr` | 配置/行情解析、零拷贝视图、内存池 |
| C++20 | 2020 | **范式升级** | concepts、ranges、coroutines、modules、`span`、`jthread`、`format` | 约束模板、惰性数据管道、线程收尾 |
| C++23 | 2023 | **实用主义补齐** | `expected`、`print`、`flat_map`、`mdspan`、`generator`、deducing this | 热路径错误处理、缓存友好容器、矩阵视图 |
| C++26 | 2026 | **前沿能力** | 反射、契约、`simd`、`linalg`、`inplace_vector`、senders | 编译期元编程、向量化、异步执行模型 |

记忆锚点：**14 补语法，17 补库，20 换范式，23 补实用，26 上前沿。**

### 4.2 学习顺序与取舍

对「基础差得较多」的情况，建议每篇按三档推进：

1. **必须会（面试 + 日常代码）**：14 的泛型 lambda/放宽 constexpr/`make_unique`；17 的结构化绑定/`if constexpr`/`optional`/`string_view`/`pmr`；20 的 concepts/ranges/`span`/`<=>`/`consteval`；23 的 `expected`/`flat_map`/`mdspan`。
2. **应该知道（能读懂现代库）**：17 的 CTAD/折叠表达式/`variant`/`filesystem`；20 的 coroutines/modules/`jthread`/`format`；23 的 deducing this/`ranges::to`。
3. **按需了解（面试加分/前沿）**：26 的反射、契约、`simd`、senders；本机工具链大多还不支持，先建立概念地图即可。

### 4.3 跨版本能力对照（同一问题的最佳工具）

| 需求 | C++14 | C++17 | C++20 | C++23 | C++26 |
|---|---|---|---|---|---|
| 编译期计算 | 放宽 constexpr | `if constexpr` | `consteval`/`constinit` | `if consteval` | 静态反射 |
| 泛型约束 | 泛型 lambda | `void_t`/SFINAE | concepts/`requires` | deducing this | — |
| 可选值 | 手写/指针 | `std::optional` | — | optional 单子操作 | optional range 支持 |
| 错误处理 | 异常/返回码 | `variant` 模拟 | — | `std::expected` | 契约 |
| 连续内存视图 | 裸指针+长度 | `string_view` | `std::span` | `mdspan` | `submdspan` |
| 容器 | — | `pmr` 分配器 | ranges | `flat_map`/`flat_set` | `hive`/`inplace_vector` |
| 并发 | `shared_lock` | `shared_mutex`/`scoped_lock` | `jthread`/`latch`/`barrier`/`atomic_ref` | — | senders/`rcu`/hazard pointer |
| 输出/日志 | `printf` | — | `std::format`/`osyncstream` | `std::print` | `std::formatter<path>` |

### 4.4 本机标准开关与工具链现状

实测环境：Apple M2 Pro / macOS 26.5.1（Darwin 25.5.0）/ Apple clang 17.0.0 / libc++ 200100。

```bash
clang++ -std=c++14 -O2 -Wall example14.cpp -o example14
clang++ -std=c++17 -O2 -Wall example17.cpp -o example17
clang++ -std=c++20 -O2 -Wall example.cpp   -o example
clang++ -std=c++23 -O2 -Wall example23.cpp -o example23
clang++ -std=c++26 -O2 -Wall -fsyntax-only probe.cpp
```

关键事实：

- Apple clang 17 **接受** `-std=c++26`，但不代表特性齐全；以 `__cpp_*` 特性测试宏和实际编译结果为准。

- 本机 libc++ 已支持 C++23 的 `expected`/`flat_map`/`mdspan`/`print`/`ranges::to`/`byteswap`/`to_underlying`，但 `move_only_function`/`generator`/`stacktrace`/`start_lifetime_as` 缺失。

- 本机以 `-std=c++26` 编译时只有零散特性可通过：pack indexing、`#embed`（Clang 扩展）、`= delete("reason")`、占位变量；`[[assume]]` 本身是 C++23 特性，在 C++26 模式下也可用。反射、契约、`<simd>`、`<linalg>`、`<inplace_vector>` 均不可用。

- macOS 的 `g++` 通常是指向 Apple clang 的别名，不要假设 GNU libstdc++ 行为；Linux 生产环境（GCC 12+）支持度普遍更好。

### 4.5 量化/HFT 使用原则

1. **生产基线选 C++20**：concepts/ranges/`span`/`jthread` 已足够；C++23 只挑 `expected`/`flat_map`/`mdspan` 等低风险库特性。
2. **热路径三不碰**：异常、RTTI、运行期分配。用 `std::expected` 返回错误，用 `std::span`/`string_view` 传视图，用 `pmr`/对象池管理内存。
3. **零开销抽象要验证**：concepts 与 ranges 的运行时开销通常为零，但编译期开销和代码膨胀真实存在；coroutine 的帧分配、`std::function` 的类型擦除、`flat_map` 的插入复杂度都要实测。
4. **ABI 边界慎传标准库类型**：libstdc++ 与 libc++ 的 `std::string`/`std::vector` 布局不保证兼容，跨 `.so` 用 C ABI 或自有 POD。
5. **编译期能算就前移**：`constexpr`/`consteval` 把参数表、哈希、查找表在编译期展开，运行时只读常量。

## 5. Example 说明

| 文件 | 标准 | 内容 |
|---|---|---|
| [example14.cpp](example14.cpp) | C++14 | 泛型 lambda、初始化捕获、变量模板、放宽 constexpr、`make_unique`、`index_sequence`、二进制/chrono 字面量、`exchange`、`shared_lock` |
| [example17.cpp](example17.cpp) | C++17 | 结构化绑定、`if constexpr`、折叠表达式、`optional`/`variant`/`any`、`string_view`、`filesystem`、`pmr`、inline 变量、CTAD、硬件干扰尺寸 |
| [example.cpp](example.cpp) | C++20 | 14/17/20 三段式特性巡礼：在 14/17 基础上补 concepts、ranges、`span`、`<=>`、`format`/`source_location`、`bit_cast`、`atomic_ref`、`jthread`/`latch` |
| [example23.cpp](example23.cpp) | C++23 | `expected` 单子链、`flat_map`、`mdspan`、`ranges::to`、`optional::transform`、`resize_and_overwrite`、`views::zip`、`std::println` |

```bash
# C++14 / C++17 / C++20 / C++23 四个版本，本机均已实测通过
clang++ -std=c++14 -O2 -Wall example14.cpp -o example14 && ./example14
clang++ -std=c++17 -O2 -Wall example17.cpp -o example17 && ./example17
clang++ -std=c++20 -O2 -Wall example.cpp   -o example   && ./example
clang++ -std=c++23 -O2 -Wall example23.cpp -o example23 && ./example23
```

预期观察点：

- `example14` 打印一行 C++14 结果：

```text
generic=42 pi=3.14159 constexpr=5050 unique=7 tuple=6 capture=42 bin=170 timeout=100 exchange=1
```

- `example17` 打印结构化绑定、`optional`/`variant`/`any`、CTAD、`pmr`、`fold` 与硬件干扰尺寸：

```text
has_size=yes binding=1 2.5 three optional=7 variant=variant any=3.14 view=view ctad=2.5 clamp=10 gcd=6 lcm=12 byte=66 path="/tmp/probe.txt" fold=10 inline=1 interference=64
```

- `example` 会分段打印「C++14 / C++17 / C++20」的验证结果，所有 `static_assert` 在编译期通过。本机实测输出：

```text
[C++14]
generic=42 pi=3.1416 constexpr=5050 unique=7 tuple=6 capture=42

[C++17]
binding: 1 2.5 three
optional=7 view=view variant=variant fold=10 ctad=2.5 inline=1

[C++20]
concept=42 ranges=20 span=5 <=>=true atomic=1 bitcast=0x3f800000 loc=int main()
```

- `example23` 打印 `expected` 链结果、`flat_map` 大小、`mdspan` 元素、`ranges::to` 容器大小；本机实测输出：

```text
expected=43 flat_map=3 mdspan=6 to=3 optional=9 text=xbc zip=66
```

- 本机缺口：`std::midpoint`（libc++ 未实现）与 `std::move_only_function`/`std::generator` 在示例中刻意不使用，避免把「标准有」误当成「本机能编」。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| C++14 最重要的三个特性？ | 泛型 lambda、放宽 constexpr、`std::make_unique`；再加变量模板与 `index_sequence` 就是完整答案 |
| `if constexpr` 和普通 `if` 的区别？ | 前者在模板实例化时丢弃分支、不要求 discarded 分支对当前类型合法；普通 `if` 两边都必须编译 |
| concepts 解决什么问题？ | 把 SFINAE 的「替换失败不是错误」变成可读约束、参与重载排序（subsumption）、错误信息直指不满足的约束 |
| ranges 是惰性的吗？ | 视图（views）惰性、算法（`ranges::sort` 等）立即执行；视图不拥有元素，注意悬垂引用 |
| `std::expected` 和异常怎么选？ | 热路径/无异常构建用 `expected`（错误是返回值，零分配）；构造/初始化等冷路径可继续用异常 |
| C++20 coroutine 能进撮合热路径吗？ | 默认不能：帧可能堆分配、恢复有间接跳转与状态机开销；除非用定制 allocator 并实测，否则热路径用回调/状态机 |
| C++26 现在能上生产吗？ | 不能。编译器/标准库支持零散，反射/契约/`simd` 在本机不可用；先当技术储备 |
| `std::span` 和 `std::string_view` 区别？ | `span` 是任意类型的连续视图（可写），`string_view` 是字符视图（只读）；两者都不拥有数据，生命周期由调用方保证 |

## 7. 深度追问记录（落盘区）

> 学习过程中每次追问按行追加；影响正文时同步更新对应小节并在「联动更新」注明。格式规范见 [AGENT.md §2.B](../../../AGENT.md)。

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|

## 8. 参考

- cppreference 版本页：[C++14](https://en.cppreference.com/w/cpp/14) / [C++17](https://en.cppreference.com/w/cpp/17) / [C++20](https://en.cppreference.com/w/cpp/20) / [C++23](https://en.cppreference.com/w/cpp/23) / [C++26](https://en.cppreference.com/w/cpp/26)（核实日期 2026-11-04）

- 编译器支持矩阵：[cppreference compiler support](https://en.cppreference.com/w/cpp/compiler_support)（核实日期 2026-11-04）

- 标准草案：[WG21 P1000R6](https://wg21.link/P1000R6)（C++26 时间表）；各特性 paper 见版本篇

- 书籍：《Effective Modern C++》（C++11/14）、《C++17 完全指南》、《C++20 完全指南》、《C++23 完全指南》

- 关联知识点：[move-semantics](../move-semantics/README.md)、[smart-pointers](../smart-pointers/README.md)、[object-model](../object-model/README.md)、[false-sharing](../../low-latency/false-sharing/README.md)、[branch-predictor](../../arch/branch-predictor/README.md)
