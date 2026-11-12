# lambda 捕获与闭包

> 模块: cpp ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-09 ｜ 更新: 2026-11-09
> 依据: [research/03](../../../research/03-tech-stack-map.md)（P1「lambda 捕获与闭包」，HFT 题库 Q13）

## 1. 一句话定位

lambda 表达式是**编译器生成的匿名函数对象（闭包）**的语法糖：捕获列表决定闭包的数据成员，函数体变成 `operator()`。理解闭包的关键是三件事——**捕获发生在定义时刻**（按值捕获是快照）、**按引用捕获存的是指针**（生命周期风险）、**闭包大小由捕获决定**（影响拷贝成本与 `std::function` 是否堆分配）。

## 2. 为什么面试官爱问

- **能追到编译器的类生成**：lambda 不是函数，是类；`[a, b]` 生成两个成员，`operator()` 默认 `const`。

- **生命周期是生产 bug 重灾区**：异步回调捕获局部变量引用、捕获 `this` 后对象已析构——都是悬垂。

- **性能直接相关**：无捕获 lambda 可退化为函数指针；有捕获 lambda 内联零成本；`std::function` 有类型擦除与潜在堆分配。

- **C++14/17/20 持续演进**：初始化捕获、泛型 lambda、`*this` 捕获、constexpr lambda、模板 lambda。

- 与 [move-semantics](../move-semantics/README.md)（移动捕获）、[smart-pointers](../smart-pointers/README.md)（`shared_from_this` 保活）联动。

## 3. 前置知识

- 函数对象（`operator()`）、函数指针、`std::function` 的基本用法。

- 值类别与移动语义：[move-semantics](../move-semantics/README.md)。

- 对象生命周期与 `this` 指针：[object-model](../object-model/README.md)。

- 智能指针所有权：[smart-pointers](../smart-pointers/README.md)。

## 4. 核心讲解

### 4.1 闭包是编译器生成的类

```cpp
int a = 1, b = 2;
auto f = [a, b](int x) { return a + b + x; };
```

编译器大致生成：

```cpp
struct __Lambda {
    int a;      // 按值捕获 → 成员
    int b;
    int operator()(int x) const { return a + b + x; }   // 默认 const
};
```

要点：

- 捕获列表 = 数据成员；函数体 = `operator()`；默认 `operator()` 是 `const`。

- 按值捕获在**定义时刻**拷贝，之后外部变化不影响闭包（本机实验 1：`a=10` 后 `by_value=3`，`by_ref=12`）。

- 闭包类型是**唯一的匿名类型**，两个写法相同的 lambda 也是不同类型；不能手写类型，只能用 `auto` 或 `std::function`。

### 4.2 捕获方式与语义

| 写法 | 语义 | 风险 |
|---|---|---|
| `[a, b]` | 按值拷贝 | 大对象拷贝成本；修改需 `mutable` |
| `[&a, &b]` | 存引用（本质指针） | 悬垂：被引用对象先销毁 |
| `[=]` | 默认按值 | 成员函数中会捕获 `this` 指针 |
| `[&]` | 默认按引用 | 悬垂风险更大 |
| `[a = expr]` | 初始化捕获（C++14） | 可用于移动捕获 |
| `[&r = x]` | 引用捕获 + 重命名 | 同上 |
| `[*this]` | 捕获对象副本（C++17） | 拷贝成本；但生命周期独立 |
| `[this]` | 捕获 `this` 指针 | 对象析构后悬垂 |

- `mutable` 让 `operator()` 变成非 const，从而修改按值捕获的成员：

```cpp
auto next = [n = 0]() mutable { return ++n; };
// next()=1, next()=2, next()=3
```

- 按值捕获的对象在闭包内是 `const`（无 `mutable` 时），不能修改。

- `[=]` 在成员函数中捕获的是 `this` 指针而不是对象副本（C++20 起 `= 捕获 this` 被弃用，推荐显式 `[this]` 或 `[*this]`）。

### 4.3 初始化捕获：移动与重命名

C++14 起可以在捕获列表里定义新变量：

```cpp
auto ptr = std::make_unique<int>(7);
auto owning = [p = std::move(ptr)] { return *p; };   // 移动捕获
// owning()=7，ptr 变为空
```

- 解决 C++11 无法捕获 move-only 类型的问题（`unique_ptr`、`std::thread`）。

- 也可以捕获表达式结果：`[factor = config.scale * 2]`，避免捕获整个对象。

- 本机实验 3 验证：`owning=7`、`ptr 为空=1`。

### 4.4 泛型 lambda：`operator()` 是模板

C++14 允许 `auto` 参数：

```cpp
auto twice = [](auto x) { return x + x; };          // operator() 是函数模板
auto forward_to = [](auto&&... args) {
    return target(std::forward<decltype(args)>(args)...);
};
```

- 每个实参类型组合实例化一份 `operator()`；本质与函数模板相同。

- `[](auto&& x)` 是万能引用，配合 `std::forward<decltype(x)>(x)` 保持值类别。

- C++20 起可以用显式模板参数：`[]<typename T>(T x) { ... }`。

### 4.5 闭包大小与布局

闭包大小 = 捕获成员的大小（含对齐），无捕获则为 1 字节（空类）：

本机实测（Apple clang 17，64 位）：

```text
sizeof([a, b])   = 8     // 两个 int
sizeof([&a, &b]) = 16    // 两个指针
sizeof([p = &a]) = 8     // 一个指针
```

- 按引用捕获存的是**指针**（8 字节），不是引用本身（引用没有对象表示）。

- 捕获大对象会让闭包变大，拷贝闭包成本上升；用 `[&]` 或捕获指针/`shared_ptr`。

- 闭包是值类型：可以拷贝、移动（除非捕获了 move-only 类型）；存入容器要可拷贝。

### 4.6 this 与 *this：生命周期分水岭

```cpp
struct Counter {
    int n = 0;
    auto by_this() { return [this] { return ++n; }; }         // 指针
    auto by_copy() { return [*this]() mutable { return ++n; }; } // 副本
};
```

本机实验 7：原对象 `n=100` 后，`by_this=101`（看到外部修改），`by_copy=1`（快照）。

- `[this]` 不延长对象生命周期；异步回调里对象已析构 → UB。解决：`shared_from_this()` 捕获强引用（[smart-pointers §4.5](../smart-pointers/README.md)）。

- `[*this]` 拷贝整个对象，生命周期独立，但拷贝成本高且看不到后续修改。

- 成员函数返回 lambda 时优先考虑 `[*this]` 或捕获所需字段，避免整个对象。

### 4.7 std::function：类型擦除的代价

`std::function` 是**可调用对象的类型擦除容器**：

```cpp
std::function<int(int)> f = [factor = 3](int x) { return factor * x; };
```

- 本机 `sizeof(std::function<int(int)>) = 32`（libc++）；内部通常是小对象缓冲区 + 函数指针表。

- 捕获小（能放进 SBO 缓冲区）时无堆分配；捕获大时堆分配（本机 `large` 捕获 64 字节 string，值 `193 = 64*3+1` 验证捕获生效）。

- 调用经过间接跳转，通常无法内联；热路径用模板参数/`auto` 接收 lambda，让编译器内联。

- 要求可拷贝；move-only 捕获（`unique_ptr`）无法放入 `std::function`，C++23 用 `std::move_only_function`（本机 libc++ 尚未实现，见 [modern-cpp/cpp23](../modern-cpp/cpp23.md)）。

### 4.8 生命周期陷阱与工程规则

**悬垂引用**：

```cpp
auto make_bad() {
    int x = 42;
    return [&x] { return x; };   // x 销毁后调用 = UB
}
```

**异步回调捕获局部引用**：

```cpp
void subscribe() {
    int count = 0;
    server.on_tick([&count] { ++count; });   // 回调可能在 subscribe 返回后执行
}
```

**this 悬垂**：对象析构后回调仍持有 `this`。

工程规则：

1. 跨作用域/异步的回调用**按值捕获**或 `shared_ptr` 保活，不用 `[&]`。

2. 捕获 `this` 的异步任务必须保证对象生命周期覆盖回调（`enable_shared_from_this`）。

3. 热路径避免 `std::function`，用模板参数 `template<class F> void run(F&& f)`。

4. 大对象用 `[p = &obj]` 或 `[sp = shared_ptr]`，不要 `[obj]`。

5. 需要修改按值捕获时加 `mutable`，但注意闭包拷贝后状态独立。

## 5. Example 说明

```bash
clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
```

本机实测输出：

```text
=== 1. 按值 vs 按引用捕获 ===
by_value=3 by_ref=12

=== 2. mutable ===
next=1 next=2 next=3

=== 3. 初始化捕获：转移 unique_ptr ===
owning=7 ptr 为空=1

=== 4. 泛型 lambda + 完美转发 ===
forward(5)=5 forward(v)=5

=== 5. 闭包大小 ===
sizeof([a,b])=8 sizeof([&a,&b])=16 sizeof([p=&a])=8

=== 6. constexpr lambda / 立即调用 ===
square(9)=81 iife=42

=== 7. this vs *this 捕获 ===
by_this=101 by_copy=1

=== 8. std::function 的类型擦除 ===
sizeof(std::function<int(int)>)=32 small(21)=42 large(1)=193
普通 lambda 可拷贝=1；std::function 要求可拷贝，move-only lambda 需 C++23 move_only_function

=== 9. 算法中的 lambda ===
降序: 9 6 5 4 3 2 1 1 偶数个数=3
```

观察点：

- 实验 1 证明按值捕获是快照、按引用捕获是实时。

- 实验 5 的 `16` 说明引用捕获存两个指针。

- 实验 7 的 `101` vs `1` 是 `this` 与 `*this` 的核心区别。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| lambda 的本质是什么？ | 编译器生成的匿名函数对象（闭包），捕获列表是成员，函数体是 `operator()` |
| 按值捕获什么时候拷贝？ | 定义 lambda 时；之后外部变量变化不影响闭包 |
| `[=]` 在成员函数里捕获什么？ | `this` 指针（不是对象副本）；C++20 起该写法被弃用，推荐 `[this]`/`[*this]` |
| `[*this]` 和 `[this]` 区别？ | 前者拷贝对象、生命周期独立；后者共享对象、可能悬垂 |
| 闭包多大？ | 等于捕获成员大小；无捕获为 1；按引用捕获存指针 |
| 如何捕获 `unique_ptr`？ | 初始化捕获 `[p = std::move(ptr)]` |
| 无捕获 lambda 能转函数指针吗？ | 能，隐式转换为函数指针；有捕获则不能 |
| `std::function` 有什么开销？ | 类型擦除、间接调用、大捕获堆分配、要求可拷贝 |
| lambda 捕获引用悬垂怎么避免？ | 跨作用域用按值/`shared_ptr`；异步任务保证生命周期 |
| constexpr lambda 哪个标准？ | C++17；常量表达式上下文中可编译期求值 |

## 7. 深度追问记录（落盘区）

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|

## 8. 参考

- cppreference: [lambda expressions](https://en.cppreference.com/w/cpp/language/lambda) / [std::function](https://en.cppreference.com/w/cpp/utility/functional/function)（核实日期 2026-11-09）

- 标准论文：[N3559](https://wg21.link/N3559)（泛型 lambda）、[P0170R1](https://wg21.link/P0170R1)（constexpr lambda）、[P0780R2](https://wg21.link/P0780R2)（包展开捕获）、[P0847R7](https://wg21.link/P0847R7)（deducing this）

- 书籍：《Effective Modern C++》Item 31-34（捕获与 lambda）；《C++17 完全指南》lambda 章节

- 关联：[move-semantics](../move-semantics/README.md)、[smart-pointers](../smart-pointers/README.md)、[modern-cpp/cpp14](../modern-cpp/cpp14.md)
