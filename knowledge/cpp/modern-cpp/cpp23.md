# C++23：实用主义补齐

> 所属主题: [modern-cpp](README.md) ｜ 标准: C++23（ISO/IEC 14882:2024，通称 C++23）｜ 本机验证: Apple clang 17 / libc++ 200100 ｜ 更新: 2026-11-04

## 1. 版本定位

C++20 搭好了新范式，但很多「日常真正需要」的东西还缺：没有标准错误类型、没有 `print`、没有扁平容器、没有多维视图、没有能返回值的生成器。C++23 是一次**实用主义补齐**：不追求范式革命，而是把 C++20 留下的工程缺口逐个填上。

一句话：**C++23 是「C++20 的最后一公里」**——`expected` 补错误处理，`print` 补输出，`flat_map` 补缓存友好容器，`mdspan` 补多维视图，deducing this 补 CRTP 的现代写法。

## 2. 特性地图

| 特性 | 关键词 | 头文件 | 面试权重 | 本机 |
|---|---|---|---|---|
| `std::expected` | 值或错误 | `<expected>` | ★★★ | 支持 |
| `std::print` / `println` | 类型安全输出 | `<print>` | ★★★ | 支持 |
| `std::flat_map` / `flat_set` | 扁平有序容器 | `<flat_map>`/`<flat_set>` | ★★★ | 支持 |
| `std::mdspan` | 多维连续视图 | `<mdspan>` | ★★★ | 支持 |
| `std::generator` | 协程生成器 | `<generator>` | ★★ | 缺失 |
| deducing this | 显式对象参数 | 语言 | ★★★ | 支持 |
| `if consteval` | 编译期分支 | 语言 | ★★ | 支持 |
| `std::unreachable` | 不可达标记 | `<utility>` | ★★ | 支持 |
| `[[assume]]` | 优化器假设 | 语言 | ★★ | 支持 |
| 多维下标 | `a[i, j]` | 语言 | ★★ | 支持（类类型） |
| `std::to_underlying` | 枚举转底层类型 | `<utility>` | ★★ | 支持 |
| `std::byteswap` | 字节翻转 | `<bit>` | ★★ | 支持 |
| `std::move_only_function` | 仅可移动的可调用对象 | `<functional>` | ★★★ | 缺失 |
| optional 单子操作 | `and_then`/`transform`/`or_else` | `<optional>` | ★★★ | 支持 |
| `std::out_ptr` / `inout_ptr` | 适配 C API | `<memory>` | ★ | 支持 |
| `std::stacktrace` | 调用栈 | `<stacktrace>` | ★★ | 缺失 |
| `std::ranges::to` | 视图物化为容器 | `<ranges>` | ★★★ | 支持 |
| `views::zip` | 多序列打包 | `<ranges>` | ★★ | 支持 |
| `views::enumerate` / `chunk` / `join_with` | 索引/分块/连接 | `<ranges>` | ★★ | 缺失 |
| `std::ranges::fold_left` | 左折叠 | `<algorithm>` | ★★ | 支持 |
| `std::string::contains` | 子串判断 | `<string>` | ★★ | 支持 |
| `resize_and_overwrite` | 原地重设字符串 | `<string>` | ★ | 支持 |
| `std::start_lifetime_as` | 安全类型双关 | `<memory>` | ★ | 缺失 |

## 3. 重点特性展开

### 3.1 `std::expected`：把错误变成返回值

```cpp
enum class Err { NotFound, Invalid };

std::expected<int, Err> parse(std::string_view s) {
    if (s.empty()) return std::unexpected(Err::Invalid);
    return std::atoi(std::string(s).c_str());
}

auto r = parse("42")
    .and_then([](int x) -> std::expected<int, Err> { return x * 2; })
    .transform([](int x) { return x + 1; });

if (!r) {
    handle(r.error());
} else {
    use(*r);
}
```

- `expected<T, E>` 要么持有 `T`，要么持有 `E`；`value()` 在错误时抛 `bad_expected_access<E>`，`operator*` 在错误时是 UB。

- 单子操作：`and_then`（返回新的 `expected`）、`transform`（返回值）、`or_else`（错误恢复）、`transform_error`（转换错误类型）。

- 热路径用法：错误类型选**轻量 enum / 小 struct**，避免在错误路径分配；不要用 `expected<std::string, std::string>` 把错误消息做成动态字符串。

- 与异常的分工：异常适合「构造/初始化/不可恢复」；`expected` 适合「可预期的业务失败 + 无异常构建 + 热路径」。不要在同一层混用两套错误模型。

### 3.2 `std::print` / `std::println`：告别 `printf`

```cpp
#include <print>

std::print("order={} price={:.2f}\n", id, price);
std::println("order={} price={:.2f}", id, price);
std::println("{}", std::vector{1, 2, 3});   // 范围格式化（实现支持时）
```

- 基于 `std::format`，类型安全、编译期检查格式串；不需要 `%d`/`%s` 手工匹配。

- `std::print` 默认输出到 `stdout`；`std::println` 追加换行。底层仍可能加锁/缓冲，热路径日志应使用自有 logger。

- 本机可用；但 `std::print` 的线程安全/刷新策略与 `std::cout` 的同步关系要看实现，别假设无锁。

### 3.3 `flat_map` / `flat_set`：缓存友好的有序容器

```cpp
std::flat_map<int, std::string> levels;
levels.insert({100, "bid1"});
levels.insert({101, "ask1"});
auto it = levels.find(100);        // O(log n)
```

- 底层是**连续排序数组**（或分段数组），查找 O(log n)，插入/删除 O(n) 搬移；遍历与缓存局部性显著优于红黑树。

- 与 `std::map` 的取舍：读多写少、批量构建后只查询 → `flat_map`；频繁插入删除 → `map`/`unordered_map`。

- 插入/删除会使迭代器/引用失效；热路径不要边遍历边插入。

- 对订单簿价格档位这类「启动时构建、运行期高频查」的结构很合适；但频繁撤单/改单仍要评估 O(n) 搬移。

### 3.4 `std::mdspan`：多维数据的零拷贝视图

```cpp
int data[6]{1, 2, 3, 4, 5, 6};
std::mdspan<int, std::extents<std::size_t, 2, 3>> grid{data};
int cell = grid[1, 2];                  // 行 1、列 2

// 运行时维度
std::mdspan<int, std::dextents<std::size_t, 2>> dynamic{data, 2, 3};
```

- `mdspan` 是「多维 span」：不拥有数据，只保存指针 + extent + layout + accessor。

- `layout_right`（行优先，C 风格）、`layout_left`（列优先，Fortran 风格）、`layout_stride`（自定义步长）。

- 适合矩阵、因子面板、订单簿二维网格；把 `mdspan` 传给数值库时可零拷贝。

- `std::submdspan` 是 C++26，本机不可用；切片目前要手动算偏移。

### 3.5 deducing this：CRTP 的现代替代

```cpp
struct Base {
    template <typename Self>
    void interface(this Self&& self) {
        self.implementation();          // 静态分派，无虚函数
    }
};

struct Derived : Base {
    void implementation() { /* ... */ }
};
```

- 显式对象参数 `this Self&&` 会推导 `Self` 的 const/引用属性；一个模板覆盖 const/非 const/左值/右值四种重载。

- 典型用途：CRTP 去虚化、递归 lambda、链式 API 返回派生类型。

- 本机 Apple clang 17 在 C++23 下支持；旧编译器/旧标准库可能没有。

### 3.6 编译期与底层小工具

```cpp
constexpr int f(int n) {
    if consteval { return n * 2; }       // 只在编译期求值时走这里
    else { return runtime_compute(n); }
}

[[assume(x >= 0)]];                     // 告诉优化器；违反则 UB
std::unreachable();                     // 标记不可达；执行到则 UB

int& cell = matrix[i, j];               // 多维下标（类类型重载 operator[]）
enum class Color { Red = 1 };
auto raw = std::to_underlying(Color::Red);  // 1
auto swapped = std::byteswap(0x12345678u);   // 0x78563412
```

- `if consteval` 取代 `if (std::is_constant_evaluated())`，语义清晰、不会被运行期分支优化误导。

- `[[assume]]`/`std::unreachable` 都是「给优化器承诺」，承诺错误就是 UB；只在确定的不变量处使用。

- `to_underlying` 让 enum 安全转底层类型，避免 `static_cast<int>` 的隐式假设；`byteswap` 用于行情/协议字节序转换。

- 多维下标只对**重载了 `operator[]` 的类类型**生效，内置数组 `arr[i, j]` 仍然不合法。

### 3.7 ranges 的实用补丁

```cpp
std::vector<int> v{1, 2, 3};
auto doubled = v | std::views::transform([](int x) { return x * 2; })
                 | std::ranges::to<std::vector>();     // 物化

for (auto [x, y] : std::views::zip(v, other)) use(x, y);

auto sum = std::ranges::fold_left(v, 0, std::plus{});
```

- `ranges::to` 把惰性视图转成具体容器，解决 C++20 只能手写 `vector(begin, end)` 的痛点。

- `views::zip` 把多个序列打包成 tuple 引用，适合回测里「价格 + 信号」同步遍历。

- `fold_left` 是 ranges 版 `accumulate`；`fold_right`、`fold_left_first` 同族。

- 本机缺口：`views::enumerate`/`chunk`/`join_with`/`cartesian_product` 缺失；`zip`/`fold_left` 可用。跨平台代码要先做特性检测。

## 4. 本机编译验证

```bash
clang++ -std=c++23 -O2 -Wall /tmp/probe23.cpp -o /tmp/probe23 && /tmp/probe23
# 输出：r=43 cell=6 fm=3 v=3 o=9 text=xbc

clang++ -std=c++23 -O2 -Wall /tmp/probe23ranges2.cpp -o /tmp/probe23ranges2 && /tmp/probe23ranges2
# 输出：66 6   （zip + fold_left）

clang++ -std=c++23 -O2 -Wall /tmp/probe23lang.cpp -o /tmp/probe23lang
# 通过：deducing this、if consteval、to_underlying、byteswap
```

本机支持矩阵（Apple clang 17 / libc++ 200100）：

| 可用 | 缺失 |
|---|---|
| `expected`、`print`、`flat_map`、`mdspan`、deducing this、`if consteval`、`unreachable`、`[[assume]]`、多维下标、`to_underlying`、`byteswap`、optional 单子、`out_ptr`、`ranges::to`、`zip`、`fold_left`、`string::contains`、`resize_and_overwrite` | `generator`、`move_only_function`、`stacktrace`、`enumerate`、`chunk`、`join_with`、`cartesian_product`、`start_lifetime_as` |

> 结论：C++23 在本机可以覆盖「错误处理 + 扁平容器 + 多维视图 + ranges 物化」四类高价值场景，但协程生成器与类型擦除回调仍要依赖第三方库（cppcoro/folly）。

## 5. 量化场景落点

- **热路径错误处理**：`expected<Tick, ParseError>` 解析行情，错误分支零分配；比异常更适合逐 tick 调用。

- **订单簿档位**：`flat_map<Price, Level>` 读多写少、缓存友好；改单频繁时退回 `map` 或自定义数组。

- **因子矩阵**：`mdspan<const double, dextents<size_t, 2>>` 零拷贝接收回测矩阵，按行/列布局选择 `layout_right`/`layout_left`。

- **回调**：`move_only_function<void(Tick)>` 捕获 `unique_ptr` 连接/策略对象（本机缺失时用 `std::function` + `shared_ptr` 或自写模板回调）。

- **数据管道**：`views::zip` 对齐价格与信号，`ranges::to` 物化结果；撮合热路径避免复杂 view 组合。

- **协议字节序**：`byteswap` 处理大端行情字段；`to_underlying` 把 enum 映射到数组下标。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| `expected` 和 `optional` 区别？ | `optional` 只表达「有/没有」；`expected` 还携带错误原因，适合错误处理 |
| `expected` 能替代异常吗？ | 在热路径/无异常构建可以；构造、资源获取等冷路径仍可用异常，别混用两套模型 |
| `flat_map` 为什么快？ | 连续内存、缓存局部性好；但插入/删除 O(n)，读多写少才划算 |
| `flat_map` 迭代器什么时候失效？ | 任何插入/删除都可能搬移元素，导致迭代器/引用失效 |
| `mdspan` 和 `span` 区别？ | `span` 一维连续；`mdspan` 多维 + 布局 + accessor，仍不拥有数据 |
| deducing this 解决什么？ | 用显式对象参数推导 const/引用，替代 CRTP 与多份 const 重载 |
| `if consteval` 和 `is_constant_evaluated` 区别？ | 前者是语法级编译期分支，语义清晰；后者是运行期 if，易被优化误导 |
| `[[assume]]` 安全吗？ | 只在确定不变量处用；违反假设是 UB，优化器可能据此删检查 |
| `views::zip` 返回什么？ | 打包后的 tuple 引用视图；底层序列生命周期必须覆盖遍历 |
| C++23 的 `print` 能进热路径吗？ | 不建议；格式化/锁/IO 都有成本，热路径用自有 logger |

## 7. 参考

- cppreference: [C++23](https://en.cppreference.com/w/cpp/23) / [expected](https://en.cppreference.com/w/cpp/utility/expected) / [flat_map](https://en.cppreference.com/w/cpp/container/flat_map) / [mdspan](https://en.cppreference.com/w/cpp/container/mdspan) / [print](https://en.cppreference.com/w/cpp/io/print) / [deducing this](https://en.cppreference.com/w/cpp/language/member_functions#Explicit_object_member_functions)

- 标准论文：[P0323R12](https://wg21.link/P0323R12)（expected）、[P1222R4](https://wg21.link/P1222R4)（flat_map）、[P0009R18](https://wg21.link/P0009R18)（mdspan）、[P2093R14](https://wg21.link/P2093R14)（print）、[P0847R7](https://wg21.link/P0847R7)（deducing this）

- 书籍：《C++23 完全指南》（Nicolai M. Josuttis）

- 关联：[cpp20](cpp20.md)（concepts/ranges 基础）、[smart-pointers](../smart-pointers/README.md)（`out_ptr` 与所有权）、[cache-hierarchy-mesi](../../arch/cache-hierarchy-mesi/README.md)（`flat_map` 的缓存局部性）
