# C++20：四大支柱与零开销抽象升级

> 所属主题: [modern-cpp](README.md) ｜ 标准: C++20（ISO/IEC 14882:2020）｜ 本机验证: Apple clang 17 / libc++ 200100 ｜ 更新: 2026-11-04

## 1. 版本定位

C++20 是继 C++11 之后最大的一次标准升级，通常概括为**四大支柱**：concepts（约束）、ranges（数据管道）、coroutines（协程）、modules（模块）。此外还有三向比较 `<=>`、`std::span`、`std::jthread`、`std::format`、`consteval`/`constinit`、`std::atomic_ref` 等一批直接服务工程的能力。

一句话：**C++20 把「模板元编程」从专家技巧变成普通工程师可读的约束，把「算法 + 容器」升级为惰性数据管道，并补上现代并发与格式化基础设施。**

## 2. 特性地图

| 特性 | 关键词 | 头文件 | 面试权重 | 本机 |
|---|---|---|---|---|
| concepts | `concept` / `requires` | 语言 | ★★★ | 支持 |
| ranges | `std::ranges` / views | `<ranges>` | ★★★ | 支持 |
| coroutines | `co_await` / `co_yield` / `co_return` | `<coroutine>` | ★★★ | 支持 |
| modules | `module` / `import` / `export` | 语言 | ★★ | 工具链不完整 |
| 三向比较 | `<=>` / `strong_ordering` | `<compare>` | ★★★ | 支持 |
| 指定初始化 | `.x = 1` | 语言 | ★★ | 支持 |
| `consteval` / `constinit` | 立即函数/静态初始化 | 语言 | ★★★ | 支持 |
| `std::span` | 连续内存视图 | `<span>` | ★★★ | 支持 |
| `std::jthread` | 自动 join + 停止令牌 | `<thread>` | ★★★ | 支持 |
| `atomic_ref` | 非原子对象的原子访问 | `<atomic>` | ★★★ | 支持 |
| `latch` / `barrier` / `semaphore` | 线程协调 | `<latch>`/`<barrier>`/`<semaphore>` | ★★ | 支持 |
| `std::format` | 类型安全格式化 | `<format>` | ★★★ | 支持 |
| `std::osyncstream` | 线程安全输出流 | `<syncstream>` | ★★ | 本机缺失 |
| `std::source_location` | 源码位置 | `<source_location>` | ★★ | 支持 |
| `std::bit_cast` | 类型安全位转换 | `<bit>` | ★★★ | 支持 |
| `std::midpoint` / `lerp` | 中点/线性插值 | `<numeric>` | ★ | midpoint 缺失 |
| `erase` / `erase_if` | 统一容器删除 | `<container>` | ★★ | 支持 |
| `starts_with` / `ends_with` | 字符串前后缀 | `<string_view>` | ★★ | 支持 |
| `[[no_unique_address]]` | 空成员零占用 | 语言 | ★★★ | 支持 |
| `[[likely]]` / `[[unlikely]]` | 分支提示 | 语言 | ★★★ | 支持 |
| `std::endian` | 字节序 | `<bit>` | ★★ | 支持 |
| `make_shared<T[]>` | 数组共享所有权 | `<memory>` | ★★ | 支持 |

## 3. 重点特性展开

### 3.1 concepts：把 SFINAE 变成可读约束

```cpp
template <typename T>
concept Number = std::integral<T> || std::floating_point<T>;

template <Number T>
constexpr T twice(T x) { return x + x; }

void process(Number auto x);          // 缩写函数模板

template <typename T>
    requires std::sortable<T>
void sort_and_check(T& range);
```

- 约束在**实例化时**检查，错误信息直接指出「`T` 不满足 `Number`」，而不是一屏 SFINAE 失败候选。

- concept 之间可以**包含/合取**（subsumption），重载排序按约束强度选择更具体的版本。

- `requires requires { ... }` 用于临时表达式约束；需要复用时写成命名 concept。

- 与 `if constexpr` 的分工：concept 决定「这个模板能不能实例化」，`if constexpr` 决定「实例化后走哪条分支」。

### 3.2 ranges：惰性视图与投影

```cpp
std::vector<int> v{1, 2, 3, 4, 5};
auto squares = v
    | std::views::filter([](int x) { return x % 2 == 0; })
    | std::views::transform([](int x) { return x * x; });

for (int x : squares) use(x);          // 惰性：循环时才计算

struct Trade { int id; double price; };
std::vector<Trade> trades;
std::ranges::sort(trades, {}, &Trade::price);   // 投影：按 price 排序
```

- **视图（views）惰性、不拥有数据**；`filter`/`transform` 返回轻量视图，底层容器销毁后视图悬垂。

- ranges 算法（`sort`/`find`/`count`）是**立即执行**；`views::iota` 可生成无限序列，必须配合 `take`。

- 投影（projection）让 `sort(v, {}, &Trade::price)` 替代 lambda，代码更短；但成员指针的可读性见仁见智。

- C++20 没有 `ranges::to`（C++23 才有）；要把视图物化成容器，需手写 `std::vector(begin, end)`。

### 3.3 coroutines：可挂起函数的机械结构

```cpp
#include <coroutine>

struct Task {
    struct promise_type {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
};

Task job() { co_return; }
```

- 编译器把函数体改写成状态机：局部变量放进**协程帧**，`co_await`/`co_yield` 处保存恢复点。

- `promise_type` 决定「开始是否挂起、结束是否挂起、返回值/异常怎么处理」；`std::coroutine_handle` 用于手动恢复/销毁。

- C++20 **没有标准任务类型**（`std::task`/`generator` 都不是 C++20）；实际项目用 cppcoro、folly::coro 或自写 Task。`std::generator` 是 C++23。

- 低延迟警告：协程帧可能堆分配，恢复是间接跳转 + 状态机分支；撮合热路径默认不要用，除非用定制 allocator 并实测尾延迟。

### 3.4 modules：编译期隔离，但工程落地仍难

```cpp
// math.cppm
export module math;
export int add(int a, int b) { return a + b; }

// main.cpp
import math;
int main() { return add(1, 2); }
```

- 模块替代头文件：`export` 控制可见性、`import` 不泄漏宏、编译一次可复用 BMI（二进制模块接口）。

- 全局模块片段 `module; #include <...>` 用于兼容旧头文件；`export import` 可再导出依赖。

- 现实问题：构建系统（CMake/MSBuild）支持不统一、ABI 与编译器实现差异、标准库模块 `import std;` 在 C++23 才标准化。**2026 年生产代码仍以头文件为主**，模块适合新项目试验。

### 3.5 三向比较、指定初始化与编译期初始化

```cpp
struct Point {
    int x, y;
    auto operator<=>(const Point&) const = default;   // 自动生成 ==、<、<=...
};

Point p{.x = 1, .y = 2};          // 指定初始化：顺序必须与声明一致

consteval int square(int n) { return n * n; }       // 每次调用都必须编译期求值
constinit static int table_size = 1024;             // 静态初始化，但不是 const
```

- `<=>` 返回 `strong_ordering`/`weak_ordering`/`partial_ordering`；浮点 NaN 只能得到 `partial_ordering`。

- 默认 `<=>` 会同时生成 `==`；自定义 `<=>` 时注意 `==` 的语义是否仍然合理。

- 指定初始化只适用于聚合体，成员顺序必须按声明顺序，禁止窄化转换。

- `consteval` 是「必须编译期」；`constexpr` 是「可以编译期，也可以运行期」；`constinit` 只保证静态初始化阶段完成，不保证不可变。

- `std::is_constant_evaluated()` 在 `if` 里用容易踩坑（编译器可能把运行期分支优化掉），C++23 的 `if consteval` 是更清晰的替代。

### 3.6 `span`、`jthread` 与并发原语

```cpp
void consume(std::span<const Tick> ticks);     // 不拥有、可指向数组/vector/内存块

std::jthread worker([](std::stop_token st) {
    while (!st.stop_requested()) {
        poll();
    }
});                                            // 析构：request_stop() + join()

std::latch ready{4};                           // 一次性倒计时
std::barrier sync{4};                          // 可复用屏障
std::counting_semaphore<8> slots{8};           // 计数信号量

int plain = 0;
std::atomic_ref<int> ref{plain};               // 给普通 int 原子语义
ref.fetch_add(1, std::memory_order_relaxed);
```

- `span` 是「指针 + 长度」的安全封装；`span<T, N>` 带编译期长度。注意它不延长生命周期，容器扩容后原 span 悬垂。

- `jthread` 析构自动请求停止并 join，避免 `std::thread` 析构时 `std::terminate`；停止是**协作式**，业务循环必须检查 `stop_requested()`。

- `atomic_ref` 要求对象对齐满足 `required_alignment`，且所有并发访问都通过 `atomic_ref`；同一对象混用普通读写仍是数据竞争。

- `latch` 一次性、`barrier` 可复用、`semaphore` 控制并发数；它们都建立在原子/等待通知之上，热路径仍要评估唤醒成本。

### 3.7 `format`、`source_location`、`bit_cast` 与容器删除

```cpp
std::string msg = std::format("order={} price={:.2f}", id, price); // 类型安全，编译期检查格式
auto loc = std::source_location::current();                       // 文件/行/函数

float f = 1.0f;
auto bits = std::bit_cast<std::uint32_t>(f);                      // 同尺寸、可平凡复制

std::erase(v, 2);                                                 // 删除所有 2
std::erase_if(m, [](const auto& kv) { return kv.second == 0; });   // 条件删除

std::string_view sv = "hello";
bool ok = sv.starts_with("he") && sv.ends_with("lo");             // C++20
```

- `std::format` 的格式串在 C++20 是编译期检查（`std::format_string`），比 `printf` 安全；`std::print` 是 C++23。

- `std::source_location` 可作默认参数捕获调用点，替代 `__FILE__`/`__LINE__` 宏。

- `bit_cast` 要求源/目标同尺寸且 trivially copyable；跨端序传输仍要显式转换，不能假设小端。

- `erase`/`erase_if` 对 vector 是 O(n) 搬移，对 map/list 是节点删除；返回删除个数。

- `[[likely]]`/`[[unlikely]]` 是给编译器的提示，不是保证；真实效果要结合 [branch-predictor](../../arch/branch-predictor/README.md) 的汇编与实测。

## 4. 本机编译验证

```bash
clang++ -std=c++20 -O2 -Wall /tmp/probe20.cpp -o /tmp/probe20 && /tmp/probe20
# 通过：concepts、ranges、span、atomic_ref、latch、jthread、format、source_location、bit_cast、<=>

clang++ -std=c++20 -O2 -Wall /tmp/probe20coro.cpp -o /tmp/probe20coro && /tmp/probe20coro
# 输出：coro ok

clang++ -std=c++20 -O2 -Wall /tmp/probe20e.cpp -o /tmp/probe20e
# 通过：erase/erase_if、starts_with/ends_with、bind_front、make_shared<int[]>、to_address
```

本机缺口（必须知道，避免把标准当实现）：

- `std::midpoint` 未实现（`__cpp_lib_midpoint` 宏缺失）；`std::lerp` 同族需实测。

- `std::osyncstream` 未实现（`<syncstream>` 头文件存在但无该类）。

- `std::string`/`std::string_view` 的 `contains` 是 C++23 特性（关联容器的 `contains` 是 C++20）；C++20 的字符串只有 `starts_with`/`ends_with`。

- modules 未验证成功：Apple clang 17 不接受 `-fmodules-ts`，C++20 命名模块需要额外构建配置，生产暂按头文件。

## 5. 量化场景落点

- **concepts 约束策略接口**：`template<PriceSource S>` 约束 `S::next()` 返回 `Tick`，编译期拒绝不满足的源，避免运行期虚调用。

- **ranges 做研究管道**：回测数据过滤/变换用 views，惰性求值减少中间容器；撮合热路径避免复杂 view 组合，防止优化器看不清。

- **span 传行情切片**：`consume(std::span<const Tick>)` 零拷贝传递一批 tick；底层 buffer 生命周期由调用方保证。

- **jthread 管理后台线程**：行情连接、日志刷盘、风控监控用 `jthread`，析构自动停止；循环内检查 `stop_token`。

- **atomic_ref 给第三方结构加原子语义**：无法改成 `std::atomic` 的共享结构（如 mmap 行情缓存）可用 `atomic_ref`，但必须全链路统一。

- **format 只用于冷路径日志**：`std::format` 有分配与格式化成本；热路径用预分配 buffer 或二进制日志。

- **likely/unlikely 配合分支预测**：在错误处理分支标 `[[unlikely]]`，但必须用 perf/汇编验证，不能凭感觉。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| concepts 和 SFINAE 的区别？ | concepts 是可读约束、参与重载排序、错误信息好；SFINAE 靠替换失败，难读难维护 |
| ranges 的 view 会拷贝元素吗？ | 不会，view 只持有迭代器/指针；`transform` 按需计算，`filter` 按需跳过 |
| ranges 最大的坑？ | 视图不拥有数据，底层容器销毁/扩容后悬垂；无限 iota 不加 take 会死循环 |
| coroutine 一定堆分配吗？ | 不一定，编译器可在已知生命周期时消除分配；但默认实现常分配，热路径要实测 |
| `co_await` 和回调有什么区别？ | 协程把续体（continuation）编译器化，代码像同步；回调需要手写状态机，容易回调地狱 |
| modules 为什么没普及？ | 构建系统/ABI/标准库模块支持不统一；头文件生态成熟，迁移收益有限 |
| `<=>` 默认生成哪些运算符？ | 默认 `<=>` 会生成 `==` 与全部关系运算符；浮点得到 `partial_ordering` |
| `consteval` 和 `constexpr` 区别？ | `consteval` 必须编译期求值；`constexpr` 允许编译期或运行期 |
| `constinit` 是 const 吗？ | 不是；它只保证静态初始化在编译期完成，变量仍可修改 |
| `atomic_ref` 能替代 `std::atomic` 吗？ | 只适合无法改类型的共享对象；对齐、生命周期、全链路一致性要求高，不能混用普通访问 |

## 7. 参考

- cppreference: [C++20](https://en.cppreference.com/w/cpp/20) / [concepts](https://en.cppreference.com/w/cpp/language/constraints) / [ranges](https://en.cppreference.com/w/cpp/ranges) / [coroutines](https://en.cppreference.com/w/cpp/language/coroutines) / [modules](https://en.cppreference.com/w/cpp/language/modules) / [span](https://en.cppreference.com/w/cpp/container/span)

- 标准论文：[P0898R3](https://wg21.link/P0898R3)（concepts）、[P0896R4](https://wg21.link/P0896R4)（ranges）、[P0912R5](https://wg21.link/P0912R5)（coroutines）、[P1103R3](https://wg21.link/P1103R3)（modules）

- 书籍：《C++20 完全指南》（Nicolai M. Josuttis）；《C++20 in Practice》

- 关联：[object-model](../object-model/README.md)（去虚化与 CRTP/concepts）、[branch-predictor](../../arch/branch-predictor/README.md)（`[[likely]]`）、[false-sharing](../../low-latency/false-sharing/README.md)（`atomic_ref` 与对齐）
