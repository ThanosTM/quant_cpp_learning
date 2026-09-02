# 智能指针与所有权（Smart Pointers & Ownership）

> 模块: cpp ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-09-02 ｜ 更新: 2026-09-02
> 依据: HFT 题库 Q10、JD「现代 C++」（[research/03](../../research/03-tech-stack-map.md)）；九坤面经考点（[interview/experiences/001](../../interview/experiences/001-quant-interview-notes.md)）

## 1. 一句话定位

智能指针用 RAII 把堆对象的生命周期绑定到栈对象上，让所有权（ownership）成为类型系统可表达、编译器可检查的语义：`unique_ptr` 表达**独占**所有权（零开销），`shared_ptr` 表达**共享**所有权（引用计数），`weak_ptr` 表达**不拥有**的观察（打破循环引用）。

## 2. 为什么面试官爱问

- 所有权选型是 C++ 工程第一题：「这个接口该收 `unique_ptr` 还是 `shared_ptr` 还是裸指针？」一问就分出工程经验。

- 每一步都能追到实现层：控制块长什么样、计数为什么必须是原子的、`make_shared` 好在哪——九坤面经直接问「智能指针线程安全性」。

- 与低延迟天然衔接：`shared_ptr` 拷贝的原子计数开销、多线程下控制块计数器就是一条被伪共享打爆的缓存行（联动 [false-sharing](../../low-latency/false-sharing/README.md)）。

## 3. 前置知识

- [move-semantics/](../move-semantics/README.md)（`unique_ptr` 是 move-only 类型，理解所有权转移的语言基础）

- RAII（规划中：raii-exception-safety/）

## 4. 核心讲解

### 4.1 所有权模型总览

| 类型              | 所有权语义                 | 开销                 | 典型用途                 |
| --------------- | --------------------- | ------------------ | -------------------- |
| `unique_ptr<T>` | 独占，move-only，作用域结束即析构 | ≈ 0（见 §4.2）        | 工厂返回值、pImpl、默认选择     |
| `shared_ptr<T>` | 共享，引用计数，最后一个离开即析构     | 控制块 + 每次拷贝 2 次原子操作 | 所有权真正多方共享、跨层缓存       |
| `weak_ptr<T>`   | 观察，不增加强计数             | 与 shared 同源        | 破环、观察者、缓存            |
| 裸指针 / 引用        | **不拥有**，不负责释放         | 0                  | 函数参数的观察视图（「我借用，不管理」） |

选型口诀：**默认** **`unique_ptr`；仅当所有权真的共享才用** **`shared_ptr`；只是观察就传裸指针或引用。**

### 4.2 `unique_ptr`：零开销与 move-only

- **大小 = 裸指针（EBO 空基类优化）**：EBO 指空类作为**基类**时可以占 0 字节（而独立空对象至少 1 字节），条件是无非静态数据成员、无虚函数、无虚基类。`unique_ptr` 把删除器作为私有基类持有（libstdc++ `__compressed_pair` 思路），因此**零开销删除器 = 空类 + 调用可内联**：默认 `delete`、无捕获 lambda、无状态 functor 都满足，`sizeof(unique_ptr<T>) == sizeof(T*)`。函数指针删除器（`void(*)(T*)`）多占 8 字节且是间接调用、阻断内联；有状态删除器（捕获了数据）则无论写成什么都要占 `sizeof(状态)` 的存储——需要状态就直接让 functor 按值携带（`unique_ptr<FILE, FileCloser>` 存 `FILE*` 用哪种写法都躲不掉）。成员位置的等价手段是 C++20 `[[no_unique_address]]`。全部结论以 `static_assert` 编译期验证：[example-ebo-deleter.cpp](example-ebo-deleter.cpp)。

- **move-only**：拷贝构造/赋值被 `=delete`，所有权转移只能 `std::move`；转移后源指针变为空（moved-from 状态安全可再赋值，见 [move-semantics §4.7](../move-semantics/README.md)）。

- **`make_unique`** **的异常安全理由**：C++17 之前 `f(widget_uptr(new T), mayThrow())` 中 `new T` 与 `mayThrow()` 的求值顺序不确定，若 `mayThrow()` 先抛出则泄漏；`make_unique` 把分配与构造封装在函数内部，杜绝该窗口（C++17 已修复求值顺序规则，但习惯依旧值得保持）。

- 数组特化 `unique_ptr<T[]>` 提供 `operator[]`，析构走 `delete[]`。

### 4.3 `shared_ptr`：控制块与原子计数

```
shared_ptr<T>（两个指针）          控制块（堆上一块管理结构）
┌────────────┐                   ┌───────────────────────────────┐
│ T* _M_ptr   │────────→          │ T 对象（make_shared 时内嵌于此）│
│ _M_refcnt   │────────→          ├───────────────────────────────┤
└────────────┘                   │ 强计数(原子) ｜ 弱计数(原子)    │
                                 │ deleter ｜ allocator           │
                                 └───────────────────────────────┘
```

- **每次拷贝 = 强计数原子递增，每次析构 = 原子递减**；递减到 0 时销毁对象，弱计数也归 0 时释放控制块。

- `shared_ptr<T>(new T)` 是**两次分配**（对象一次、控制块一次）；`make_shared<T>` 合并为**一次分配**，对象与控制块相邻 → 缓存局部性更好，且天然异常安全。代价：只要还有 `weak_ptr` 存活，对象那部分内存（含 `sizeof(T)`）就不能回收——对象大而弱引用长存时要留意。

- `weak_ptr` 的存在正是弱计数存在的原因：观察者不阻止对象析构，但 `lock()` 时能通过弱计数判断控制块还活着，从而安全地检查-提升（`lock()` 内部是原子的「读强计数并自增」比较交换，避免检查与使用之间被其他线程析构的竞态）。

### 4.4 `weak_ptr`：打破循环引用

两个对象互持 `shared_ptr`（父子互指、双向链表节点、观察者注册表），强计数互相顶住，**永远到不了 0** → 内存泄漏。修法：把「反向/回指」一侧改成 `weak_ptr`，只保留一个方向的所有权。判断标准很简单：**所有权是树状/单向的（只有一个方向算数），回边一律 weak。**

### 4.5 线程安全边界与 `enable_shared_from_this`

三条边界一次说清（面试高频，九坤原题）：

1. **控制块计数是原子的** → 多个线程各自持有/拷贝/析构**不同的** **`shared_ptr`** **副本**（指向同一对象）是安全的（example 实验四验证）。
2. **同一个** **`shared_ptr`** **实例**被多线程并发读写（一个线程 `reset`，另一个线程拷贝）**不安全** → 需要外部同步或传值拷贝。
3. **所指对象本身**的线程安全性与智能指针无关 → `shared_ptr<vector<int>>` 不让 `push_back` 变成线程安全。

对象内部需要把自己交给外部共享时，**不能** `shared_ptr<T>(this)`——会为同一对象造出第二个控制块，双重释放。正确做法是继承 `enable_shared_from_this<T>` 后调用 `shared_from_this()`：其实现是对象内部藏了一个指向自己控制块的 `weak_ptr`（由**第一个**拥有它的 `shared_ptr` 构造函数填充），要求对象**必须已被** `shared_ptr` 管理（否则抛 `bad_weak_ptr`）。

三个易踩的坑（实测见 [example-shared-from-this.cpp](example-shared-from-this.cpp)）：① **构造函数内**调用 `shared_from_this()` 必抛 `bad_weak_ptr`——weak 还没被填充 → 需要"自共享"就用工厂函数两段式初始化；② `p1 == p2` 只比较裸指针，**检不出双控制块**——鉴别要用 `owner_before`（两个方向都返回 false 才是同一控制块）；③ 这个内嵌 weak 让每个对象恒定多 16 字节。C++17 起 `weak_from_this()` 提供不抛异常的访问。最典型场景：把成员函数丢进线程池/异步回调时，lambda 里捕获 `shared_from_this()` 持强引用，保证对象活到回调执行完。

### 4.6 低延迟视角：热路径上的 `shared_ptr`

- 一次 `shared_ptr` 拷贝/析构 = 一次**原子读-改-写**，多核上意味着控制块缓存行在核间乒乓（机理见 [false-sharing](../../low-latency/false-sharing/README.md)），竞争激烈时单次可达百纳秒级——比一次函数调用贵 1-2 个数量级。

- 因此行情/撮合热路径的惯例：**所有权在单一所有权边界内用** **`unique_ptr`** **或值/裸指针传递**，把 `shared_ptr` 的共享约束在冷路径（初始化、订阅管理、配置分发）。

- `make_shared` 优于 `shared_ptr(new T)` 不仅是分配次数：对象与控制块同缓存行附近，首次解引用更少 miss。

- 引用计数递减发生在析构路径上——用对象池 + 显式生命周期管理（返回裸指针观察）是 HFT 中更常见的取舍（联动 low-latency/memory-pool/，规划中）。

## 5. Example 说明

```bash
g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
```

四个实验，预期观察点：

1. **零开销验证**：`sizeof(unique_ptr<Widget>)` = 8 = 裸指针；函数指针删除器版本 = 16。
2. **分配次数**：`shared_ptr(new Counted)` 2 次分配（对象+控制块），`make_shared` 1 次（重载全局 `operator new` 统计）。
3. **循环引用**：双 `shared_ptr` 互指离开作用域后无析构输出（泄漏）；一侧改 `weak_ptr` 后析构顺序正常打印。
4. **计数原子性**：4 线程各自并发拷贝/析构副本各 10 万次，结束后 `use_count()` 精确回到 1，无丢失或重复释放。

## 6. 高频面试问答

| 问题                          | 30 秒速答                                                              |
| --------------------------- | ------------------------------------------------------------------- |
| 何时用 unique vs shared？       | 默认 unique（90% 场景）；仅所有权真正共享时用 shared，其拷贝有原子计数开销                      |
| shared\_ptr 线程安全吗？          | 分三层：控制块计数原子（多线程各持副本安全）；同一实例并发读写不安全；所指对象与智能指针无关                      |
| 循环引用怎么破？                    | 一侧改 weak\_ptr；weak 不增加强计数，访问时 lock() 原子提升                           |
| make\_shared 优点与缺点？         | 一次分配、异常安全、缓存友好；缺点：weak 长存会延迟对象内存回收                                  |
| unique\_ptr 为什么是零开销？        | 无状态删除器被空基类优化吃掉，sizeof == 裸指针；move-only 编译期保证唯一所有权                   |
| 为什么不能 shared\_ptr<T>(this)？ | 会创建第二个控制块导致双重释放；用 enable\_shared\_from\_this，前提是对象已被 shared\_ptr 管理 |
| 什么时候用裸指针？                   | 只观察不拥有：函数参数/返回值表示借用，不参与生命周期管理                                       |

## 7. 深度追问记录（落盘区）

学习过程中的追问，按行追加；结论影响主文时同步改正文并注明。

| 日期         | 问题                                 | 结论摘要                                                                                                                                                                                                                                                                                        | 联动更新                                                               |
| ---------- | ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| 2026-09-02 | 怎样的 `unique_ptr` 删除器零开销？EBO 是什么机制？ | 零开销两条件：删除器是**空类**（无状态 functor、无捕获 lambda、`default_delete`）且调用**可内联** → `sizeof(unique_ptr<T>) == sizeof(T*)`。EBO = 空类作基类占 0 字节（独立对象至少 1 字节；条件：无非静态成员/无虚函数/无虚基类），`unique_ptr` 以私有基类持有删除器（compressed-pair 思路）。函数指针删除器 +8 字节且阻断内联；有状态删除器必占 `sizeof(状态)`。成员位置等价物：C++20 `[[no_unique_address]]`  | §4.2 首条扩写；example-ebo-deleter.cpp（static\_assert 全验证）              |
| 2026-09-02 | 手写一个 SharedPtr，核心机制是什么？            | 控制块 = `{atomic strong, atomic weak, T*}`；weak 初始 1 代表「全体强引用」的**集体份额**。拷贝 = relaxed `fetch_add`；最后一个 release 用 acq\_rel `fetch_sub`：先 delete 对象，再对 weak 做集体递减，weak 也归 0 才释放控制块。`weak::lock()` = CAS 循环（acquire）防「检查-使用」竞态。与 std 的三处差距：合并计数成一个字、deleter 类型擦除（虚函数）、`make_shared` 内嵌对象          | example-my-shared-ptr.cpp（5 实验：拷贝/移动/weak 生命周期/循环引用/4 线程 + sizeof） |
| 2026-09-02 | `unique_ptr<T[]>` 的用法与坑？           | 偏特化使默认删除器走 `delete[]`，提供 `operator[]`（无越界检查）；**不存长度**，遍历用 `std::span`；`make_unique<T[]>(n)` 值初始化清零。大坑：`unique_ptr<T>(new T[n])` 漏写 `[]` 能编译，但 `delete` 与 `new[]` 不匹配 = UB（ASan 可检出）                                                                                                         | §4.2 数组条目扩写；example-unique-array.cpp                               |
| 2026-09-02 | `enable_shared_from_this` 的正确用法与坑？ | 内嵌 `weak_ptr<T>` 由第一个拥有它的 shared\_ptr 填充，`shared_from_this()` 即 lock。坑：①构造函数内调用抛 `bad_weak_ptr`（改用工厂函数两段式）；②`shared_ptr<T>(this)` 造第二个控制块 → 双重释放，且 `p1==p2` 检不出（只比裸指针），要用 `owner_before` 双向 false 鉴别；③每对象恒多 16 字节；C++17 `weak_from_this()` noexcept。范式：异步回调捕获 `shared_from_this()` 持强引用保证存活 | §4.5 补三坑；example-shared-from-this.cpp                              |

## 8. 参考

- cppreference: [unique\_ptr](https://en.cppreference.com/w/cpp/memory/unique_ptr) / [shared\_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr) / [weak\_ptr](https://en.cppreference.com/w/cpp/memory/weak_ptr) / [enable\_shared\_from\_this](https://en.cppreference.com/w/cpp/memory/enable_shared_from_this)（核实日期 2026-09-02）

- 《Effective Modern C++》Item 18-22（智能指针专章）

- [quantt Q10 Smart pointers](https://www.quantt.co.uk/resources/cpp-quant-interview-questions)（核实日期 2026-09-02）

- 关联面经：[interview/experiences/001](../../interview/experiences/001-quant-interview-notes.md)（九坤「智能指针线程安全性」已并入本文 §4.5）

