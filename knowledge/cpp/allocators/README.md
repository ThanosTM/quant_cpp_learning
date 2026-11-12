# 分配器与内存池

> 模块: cpp ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-09 ｜ 更新: 2026-11-09
> 依据: [research/03](../../../research/03-tech-stack-map.md)（P1「分配器 / 内存池 / placement new」，HFT 题库 Q20；与 [low-latency](../../low-latency/README.md) 交叉）

## 1. 一句话定位

分配器（allocator）是 STL 容器与内存来源之间的**策略接口**：容器只调用 `allocate/deallocate`，不关心内存从哪来。内存池（memory pool）则是在 allocator 之下、用**预分配 + 复用**替代频繁 `malloc/free` 的实现手法。C++17 的 `std::pmr` 把这件事标准化：`memory_resource` 提供运行期多态，`monotonic_buffer_resource`/`pool_resource` 提供现成策略。核心目标：**降低分配次数、提升局部性、让延迟可预测**。

## 2. 为什么面试官爱问

- **能追到 `operator new` 与 malloc**：一次 `new` 背后是分配器、对齐、元数据、系统调用（必要时）。

- **低延迟岗位的必答题**：热路径 `malloc` 的延迟抖动远大于算法本身；HFT 题库 Q20 直接考内存池。

- **能区分「会用容器」与「理解容器」**：`vector` 的扩容、`map` 的节点分配、`pmr` 的资源传播都依赖 allocator 模型。

- **缓存行对齐与伪共享**：`alignas(64/128)`、over-aligned 类型、对象池布局直接映射 [false-sharing](../../low-latency/false-sharing/README.md)。

- 与 [stl-containers](../stl-containers/README.md)（节点分配）、[object-model](../object-model/README.md)（布局与对齐）联动。

## 3. 前置知识

- `new/delete`、`operator new/delete`、placement new 的基本语法。

- 对象生命周期：构造/析构、存储期、显式析构。

- 对齐（`alignof`/`alignas`）与对象布局：[object-model §4.1](../object-model/README.md)。

- 容器扩容与节点分配：[stl-containers §4.1](../stl-containers/README.md)。

- 内存序与无锁基础（对象池常用于无锁队列）：[low-latency](../../low-latency/README.md)。

## 4. 核心讲解

### 4.1 allocator 模型：容器只认三个操作

一个最小分配器需要：

```cpp
template <typename T>
struct Alloc {
    using value_type = T;
    T* allocate(std::size_t n);              // 分配 n 个 T 的未初始化存储
    void deallocate(T* p, std::size_t n);    // 释放
    // 可选：is_always_equal、propagate_on_*、select_on_container_copy_construction
};
```

- 容器通过 `std::allocator_traits<Alloc>` 访问分配器，自动补全 `construct/destroy`、`rebind`、传播 traits。

- `allocate` 返回**未初始化内存**，不调用构造函数；容器自己 placement new。

- C++17 起 `allocate` 可接受对齐；C++20 用 `std::allocator<T>::allocate` 保证 `alignof(T)`。

- `rebind<U>::other` 让 `map` 把 `allocator<pair<const K,V>>` 转成节点类型分配器——标准库内部大量使用。

本机实验 1：`vector<int, CountingAllocator<int>>` 插入 100 个元素触发 **8 次分配**（2 倍扩容），`reserve(100)` 只有 **1 次**。

### 4.2 为什么需要自定义分配器

| 动机 | 手段 |
|---|---|
| 减少 `malloc` 次数 | 批量预分配（arena/monotonic） |
| 降低延迟抖动 | 固定大小池、预分配、无锁 free list |
| 提升局部性 | 连续分配、SoA、缓存行对齐 |
| 共享内存/持久化 | 自定义 `allocate` 从 mmap/共享内存取 |
| 统计/调试 | 计数、泄漏检测、越界检查 |
| 隔离 | 按模块/线程划分内存，避免交叉污染 |

### 4.3 `std::pmr`：运行期多态内存资源

`std::pmr::memory_resource` 是抽象基类：

```cpp
class memory_resource {
    virtual void* do_allocate(std::size_t bytes, std::size_t alignment) = 0;
    virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) = 0;
    virtual bool do_is_equal(const memory_resource&) const noexcept = 0;
};
```

三种标准资源：

| 资源 | 行为 | 适用 |
|---|---|---|
| `monotonic_buffer_resource` | 只增不减，整块释放；单对象释放是 no-op | 请求/帧级短生命周期批量分配 |
| `unsynchronized_pool_resource` | 按大小分级池化，非线程安全 | 单线程、频繁小块分配 |
| `synchronized_pool_resource` | 线程安全版本（内部加锁） | 多线程共享，但热路径仍有锁成本 |

用法：

```cpp
std::array<std::byte, 4096> buffer{};
std::pmr::monotonic_buffer_resource resource{buffer.data(), buffer.size()};
std::pmr::vector<int> v{&resource};      // 内存来自 buffer
```

本机实验 2 实测（libc++）：`vector` 数据偏移 3696、`pmr::string` 偏移 3488，都在 4096 字节栈缓冲区内部；libc++ 的 monotonic 资源**从缓冲区尾部向前分配**，GCC 可能从头分配——不要依赖方向。

### 4.4 对象池：固定大小 + free list

```cpp
class OrderPool {
    alignas(Order) std::byte storage_[kCapacity * sizeof(Order)];
    bool used_[kCapacity];
public:
    Order* allocate(...) { return new (slot) Order{...}; }
    void deallocate(Order* p) { p->~Order(); used_[index] = false; }
};
```

- 内存来自对象自身（栈/成员数组），运行期**零 malloc**；地址稳定，适合无锁队列/侵入式链表。

- 本机实验 3：两个 `Order` 地址间距 32 字节（`sizeof(Order)`），释放后能复用同一槽位。

- free list 可用「空闲槽位的内存本身存 next 指针」实现零额外开销；但要注意对齐与生命周期（用 `std::launder` 或 `memcpy` 存取指针）。

- 多线程池需要原子 free list 或每线程独立池；ABA 问题见无锁结构资料（[low-latency](../../low-latency/README.md)）。

### 4.5 placement new 与显式析构

```cpp
alignas(64) std::byte storage[sizeof(CacheLineAligned)];
auto* obj = new (storage) CacheLineAligned{42};   // 在已有内存上构造
obj->~CacheLineAligned();                          // 必须显式析构
```

- `placement new` 不分配内存，只调用构造函数；返回的指针可用于访问对象。

- 必须手动调用析构；否则资源（文件、锁、堆内存）泄漏。

- `std::launder`：当同一块存储被复用为不同类型的对象时，旧指针需要 `std::launder` 才能合法访问新对象（C++17）。

- 对齐：`alignas(64)` 保证存储起始地址是 64 的倍数；本机实验 4 实测 `地址%64=0`。

### 4.6 对齐与 over-aligned 类型

- C++17 起 `new` 支持 over-aligned 类型（`alignas(64)` 以上），`operator new(size_t, align_val_t)` 由编译器调用。

- 本机 `std::make_unique<CacheLineAligned>()` 返回地址满足 64 字节对齐（实验 4）。

- 缓存行对齐用于避免伪共享：每个线程的计数器/队列头尾各占一行（[false-sharing](../../low-latency/false-sharing/README.md)）。

- 注意：`std::vector<alignas(64) T>` 在 C++17 前不保证元素对齐；C++17 起 allocator 支持 over-aligned，但仍建议实测。

- 结构体内部填充（padding）会增加 `sizeof`；对齐要求越高，数组占用越大。

### 4.7 热路径分配策略

1. **预分配**：启动时 `reserve`/`resize` 到最大容量；运行期只改内容。

2. **arena/单调分配**：按请求/帧重置，不做单对象释放；适合解析、回测批次。

3. **固定大小池**：订单、连接、缓冲区等生命周期可控的对象。

4. **对象复用**：无锁队列的节点池、`freelist`；避免 ABA 用 tagged pointer/hazard pointer。

5. **避免隐式分配**：`std::function` 大捕获、`std::string` 拼接、`unordered_map` rehash、`shared_ptr` 控制块。

6. **测量**：用 `perf`/自建计数确认分配次数与尾延迟，不要凭感觉。

### 4.8 常见坑

| 坑 | 说明 |
|---|---|
| pmr 资源生命周期短于容器 | 资源销毁后容器仍持有指针 → UB；资源必须活得更久 |
| monotonic 单对象释放无效 | `deallocate` 是 no-op，内存只在资源销毁时整体释放 |
| pool 非线程安全 | `unsynchronized_pool_resource` 多线程用会数据竞争 |
| placement new 忘记析构 | 资源泄漏；析构顺序错误也会 UB |
| 对齐不匹配 | `allocate` 返回的内存不满足 `alignof(T)` → UB；用 `std::align`/aligned new |
| allocator 传播误解 | 容器赋值/swap 时分配器是否传播由 traits 决定 |
| `std::function` 隐式堆分配 | 大捕获超过 SBO 缓冲区会分配；热路径用模板参数 |

## 5. Example 说明

```bash
clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
```

本机实测输出：

```text
=== 1. 自定义分配器统计 ===
push_back(100): allocations=8 capacity=128
reserve(100):  allocations=1 capacity=100

=== 2. pmr::monotonic_buffer_resource ===
vector 在缓冲区内=1 (off=3696)，string 在内部=1 (off=3488)，缓冲区=4096 字节
本机 libc++ 从缓冲区尾部向前分配（实现相关，不要依赖方向）

=== 3. 固定容量对象池 ===
a=... b=... 间距=32 池内分配次数=2
释放后复用同一槽位=1 id=3

=== 4. placement new 与对齐 ===
placement new: value=42 地址%64=0
make_unique<alignas(64)>: 地址%64=0
```

观察点：

- 分配次数 8 vs 1：`reserve` 是热路径最便宜的优化。

- pmr 的两个对象都落在栈缓冲区，说明没有走全局堆。

- 对象池地址间距等于 `sizeof(Order)`，释放后立即复用。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| allocator 的作用？ | 把容器的内存来源抽象成 allocate/deallocate 策略，容器通过 allocator_traits 使用 |
| 为什么 `map` 的分配器类型和 `pair` 不同？ | 红黑树分配的是节点（含指针/颜色），用 `rebind` 转换 |
| `pmr` 是什么？ | C++17 多态内存资源；`memory_resource` 虚接口 + 三种标准资源 |
| `monotonic_buffer_resource` 适合什么？ | 短生命周期批量分配、整块释放；单对象释放无效 |
| pool resource 线程安全吗？ | `unsynchronized_pool_resource` 否，`synchronized_pool_resource` 是（有锁） |
| placement new 是什么？ | 在已有内存上调用构造函数；不分配内存，必须显式析构 |
| 为什么需要对象池？ | 避免热路径 malloc、地址稳定、提升局部性、延迟可预测 |
| `alignas(64)` 有什么用？ | 缓存行对齐，避免伪共享；C++17 起 new 支持 over-aligned |
| 分配器传播 traits 是什么？ | 容器拷贝/移动/swap 时是否带走分配器，由 `propagate_on_*` 决定 |
| 热路径如何避免隐式分配？ | 预分配、arena、对象池、避免 `std::function`/字符串拼接/容器 rehash |

## 7. 深度追问记录（落盘区）

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|

## 8. 参考

- cppreference: [allocator](https://en.cppreference.com/w/cpp/memory/allocator) / [allocator_traits](https://en.cppreference.com/w/cpp/memory/allocator_traits) / [memory_resource](https://en.cppreference.com/w/cpp/memory/memory_resource) / [monotonic_buffer_resource](https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource)（核实日期 2026-11-09）

- 标准论文：[N3916](https://wg21.link/N3916)（pmr）、[P0035R4](https://wg21.link/P0035R4)（aligned new）

- 书籍：《Effective STL》Item 10-11；《C++17 完全指南》pmr 章节；《Optimized C++》

- 关联：[stl-containers](../stl-containers/README.md)、[object-model](../object-model/README.md)、[false-sharing](../../low-latency/false-sharing/README.md)、[smart-pointers](../smart-pointers/README.md)
