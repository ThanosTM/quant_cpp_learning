# STL 容器底层：map / unordered_map / vector

> 模块: cpp ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-09 ｜ 更新: 2026-11-09
> 依据: [research/03](../../../research/03-tech-stack-map.md)（P0「STL 容器底层：map vs unordered_map、vector 扩容、迭代器失效」）；富途真题（map/unordered_map 底层）

## 1. 一句话定位

STL 容器的性能差异主要来自**内存布局**：`vector` 是一段连续数组（扩容要搬家），`map` 是每个节点单独 `new` 的红黑树（查找 O(log n) 且频繁 cache miss），`unordered_map` 是桶数组 + 链表/节点（平均 O(1) 但受哈希与负载因子影响）。面试的重点不是背复杂度表，而是能解释**为什么这样布局、什么时候迭代器失效、什么时候该换容器**。

## 2. 为什么面试官爱问

- **一行代码能追到内存布局**：`m[1]` 背后是红黑树旋转/哈希桶查找；`v.push_back` 背后是扩容、搬移与迭代器失效。

- **复杂度不等于实际性能**：`unordered_map` 理论 O(1)，但哈希冲突、节点分配、cache miss 会让它在小数据量下输给 `vector` 线性查找。

- **迭代器失效是生产事故高发区**：遍历 `vector` 时 `erase`、`map` 遍历时 `erase`、`unordered_map` rehash 后旧指针——都是真实 bug。

- **低延迟场景直接映射**：订单簿价格档位用 `map` 还是 `flat_map`/有序数组？行情快照用 `vector` 还是 `deque`？答案取决于插入/删除/遍历的比例与缓存局部性。

## 3. 前置知识

- 指针、引用、数组与动态内存基础。

- 模板与迭代器概念（至少知道 `begin/end`、`operator*`、`operator++`）。

- 算法复杂度记号（O(1)/O(log n)/O(n)）。

- 建议先读 [object-model](../object-model/README.md) §4.1（对象大小与对齐），本文的节点布局会用到。

## 4. 核心讲解

### 4.1 vector：连续内存 + 几何扩容

`vector` 只有三个指针（libc++/libstdc++ 通常 24 字节）：`begin`、`end`、`capacity_end`。`size()` 是 `end - begin`，`capacity()` 是 `capacity_end - begin`。

扩容规则（本机 libc++ 实测，`push_back` 逐个插入）：

```text
size:     1  2  3  4  5  6  7  8  9 ... 16 17
capacity: 1  2  4  4  8  8  8  8 16 ... 16 32
```

- 容量按 **2 倍**增长（MSVC 是 1.5 倍），保证 `push_back` 均摊 O(1)。

- 扩容 = 分配新内存 + 把旧元素**移动或拷贝**过去 + 释放旧内存；`noexcept` 移动构造才会用移动（见 [move-semantics](../move-semantics/README.md) §4.4）。

- 本机实测：`push_back` 插入 1000 个元素触发 **11 次分配**；`reserve(1000)` 只有 **1 次**。热路径已知规模时先 `reserve`。

代价与取舍：

| 操作 | 复杂度 | 说明 |
|---|---|---|
| `push_back` | 均摊 O(1) | 扩容时 O(n) |
| 中间 `insert/erase` | O(n) | 搬移后续元素 |
| 随机访问 | O(1) | 连续内存，缓存友好 |
| 查找 | O(n) | 无序；有序可二分 |

### 4.2 迭代器失效：规则来自内存布局

`vector` 的迭代器本质是**裸指针**，所以：

- 扩容后：所有迭代器、指针、引用失效（内存已换）。

- `insert/erase` 后：操作位置**之后**的迭代器失效（元素搬移）。

- `reserve` 只改容量：扩容则全部失效。

`map`/`set`/`list` 的迭代器指向**节点**，节点地址稳定：

- 插入不影响其他节点；删除只让被删节点的迭代器失效。

`unordered_map` 介于两者之间：

- rehash（负载因子超限或显式 `rehash/reserve`）后所有迭代器失效，但**指向元素的引用/指针仍有效**（节点没搬家，桶数组搬了）。

- 删除只让被删元素失效。

工程规则：**遍历中删除**用 `it = c.erase(it)`；`vector` 批量删除用 erase-remove 惯用法或 C++20 `std::erase_if`。

### 4.3 map：红黑树与节点分配

`std::map` 通常是红黑树（libc++/libstdc++ 都是），每个元素一个节点：

```text
map node: [ left | right | parent | color | pair<const K, V> ]
```

- 查找/插入/删除 O(log n)，树高约 `2*log2(n)`；每次比较都是一次指针追逐，**cache miss 是主要成本**。

- 插入要 `new` 一个节点（可能多次分配），内存碎片与分配器开销真实存在。

- 有序遍历是强项：订单簿按价格从优到劣遍历、区间查询（`lower_bound/upper_bound`）。

- `operator[]` 会**默认构造**缺失值再返回引用；只查不改用 `find` 或 `at`。

本机实测（20 万键、10 万次随机查找，3 次取最优）：`map` 约 **49.7 ns/lookup**；`unordered_map` 约 **3.7 ns/lookup**，差 13 倍。差距几乎全部来自节点跳转与 cache miss，而不是比较次数。

### 4.4 unordered_map：桶、哈希与负载因子

典型实现：`bucket array`（每个桶是指针）+ 节点链表/单链表。

- 查找 = `hash(key) % bucket_count` → 桶内比较；平均 O(1)，最坏 O(n)（全部冲突）。

- 默认 `max_load_factor()` 通常是 1.0；插入导致 `size/bucket_count > max_load_factor` 时触发 rehash（桶数增加、重新分布）。

- `reserve(n)` 预留至少容纳 n 个元素的桶数，避免多次 rehash；`rehash(n)` 直接指定桶数。

- 哈希函数质量决定实际性能：`std::hash<int>` 对连续整数很好；自定义 key 要保证分布均匀，且**相等对象哈希相同**。

- 有序遍历不存在；需要按 key 顺序时选 `map` 或排序后的 `vector`。

### 4.5 选型：先看访问模式，再看复杂度

| 场景 | 首选 | 理由 |
|---|---|---|
| 顺序追加、随机访问、批量遍历 | `vector` | 连续内存、缓存友好、无节点分配 |
| 按 key 有序、区间查询、稳定迭代器 | `map`/`set` | 红黑树，O(log n)，中序遍历有序 |
| 纯点查、无顺序需求、key 分布均匀 | `unordered_map` | 平均 O(1)，但注意 rehash 与哈希质量 |
| 读多写少、构建后只查询 | 排序 `vector` + 二分 / `flat_map`（C++23） | 连续内存，查找与遍历都更快 |
| 频繁中间插入/删除、迭代器稳定 | `list`/`map` | 节点不搬家；但遍历 cache 不友好 |
| 固定容量热路径 | `std::array` / `inplace_vector`（C++26） | 零分配、可预测 |

经验法则：

1. n 很小（几十以内）时，`vector` 线性查找常常快过 `map`（常数因子与 cache 局部性）。

2. 需要「按价格档位遍历」→ `map`；需要「按订单 id 点查」→ `unordered_map`；两者都要 → 两套索引或 `flat_map`。

3. 热路径不要边遍历边插入；先收集再批量处理。

### 4.6 string 的 SSO：为什么 `sizeof(std::string)` 是 24

libc++ 的 `std::string` 采用**短字符串优化（SSO）**：对象内部有一块小缓冲区，短字符串不分配堆内存。

本机实测：

```text
sizeof(std::string) = 24
"short"（5 字符）    capacity=22  sso=yes（data 指向对象内部）
63 字符长串          capacity=63  sso=no （data 在堆上）
```

- 24 字节 = 指针(8) + size(8) + 联合体（capacity 或小缓冲区）。短串把内容直接放在对象里。

- 影响：`std::string` 按值传递会复制 24 字节 + 可能堆分配；热路径用 `std::string_view` 传视图。

- 不同标准库的 SSO 容量不同（libstdc++ 通常 15），不要写依赖具体容量的代码。

## 5. Example 说明

```bash
clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
```

六个实验与本机实测（Apple M2 Pro / Apple clang 17 / libc++ 200100）：

1. `vector` 扩容序列：`1/1 2/2 3/4 ... 16/16 17/32`，确认 2 倍增长。

2. 扩容导致 `data()` 指针变化（`moved=yes`）。

3. 分配次数：`push_back(1000)` → 11 次；`reserve(1000)` → 1 次。

4. 查找性能：`map` 49.7 ns/lookup vs `unordered_map` 3.7 ns/lookup（20 万键、10 万次查询、3 轮取最优）。

5. `sizeof(std::string)=24`，短串 SSO 容量 22。

6. 迭代器失效规则：`vector` 扩容全部失效，`map` 插入不影响其他节点，`vector::erase` 使其后迭代器失效。

注意事项：

- 计时受机器负载影响，量级与倍数关系比绝对值更重要；复测时固定 CPU 频率/关闭后台任务。

- 不要真的解引用失效迭代器来「验证」——那是 UB，示例只观察容量与指针变化。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| `vector` 扩容为什么是 2 倍？ | 保证 `push_back` 均摊 O(1)；倍数太小分配频繁，太大浪费内存。MSVC 用 1.5 倍以更好复用旧内存 |
| `vector` 扩容时元素怎么搬？ | 有 `noexcept` 移动构造就移动，否则拷贝（强异常安全）；搬完释放旧内存 |
| `map` 和 `unordered_map` 底层？ | `map` 红黑树（有序、O(log n)、节点分配）；`unordered_map` 哈希桶 + 链表（平均 O(1)、可能 rehash） |
| 什么时候 `unordered_map` 反而慢？ | 哈希冲突严重、key 分布差、频繁 rehash、数据量小（节点分配与 cache miss 占主导） |
| `vector` 迭代器什么时候失效？ | 扩容全部失效；`insert/erase` 使操作位置之后失效；`reserve` 扩容则全部失效 |
| `map` 插入会让迭代器失效吗？ | 不会；删除只让被删节点失效 |
| `unordered_map` rehash 后什么失效？ | 所有迭代器失效，但指向元素的引用/指针仍有效（节点未搬家） |
| `map::operator[]` 有什么坑？ | key 不存在会插入默认值；只查用 `find`/`at`，避免意外扩容 |
| 为什么 `sizeof(std::string)` 是 24？ | 指针 + size + SSO 缓冲区/capacity 联合体；短串不分配堆内存 |
| 订单簿价格档位用什么容器？ | 读多写少、需有序遍历 → 有序数组/`flat_map`；频繁增删 → `map` 或自定义结构 |

## 7. 深度追问记录（落盘区）

> 学习过程中的追问按行追加；影响正文时同步更新对应小节并在「联动更新」注明。

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|

## 8. 参考

- cppreference: [vector](https://en.cppreference.com/w/cpp/container/vector) / [map](https://en.cppreference.com/w/cpp/container/map) / [unordered_map](https://en.cppreference.com/w/cpp/container/unordered_map) / [iterator invalidation](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)（核实日期 2026-11-09）

- 标准：[N3337](https://wg21.link/N3337)（C++11 容器要求）、[unordered_map 负载因子](https://en.cppreference.com/w/cpp/container/unordered_map/max_load_factor)

- 书籍：《STL 源码剖析》（侯捷）；《Effective STL》Item 1-9

- 关联：[move-semantics](../move-semantics/README.md)（扩容搬移）、[object-model](../object-model/README.md)（对象布局）、[allocators](../allocators/README.md)（节点分配与 pmr）
