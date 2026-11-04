# C++14：把 C++11 补完整

> 所属主题: [modern-cpp](README.md) ｜ 标准: C++14（ISO/IEC 14882:2014）｜ 本机验证: Apple clang 17 / libc++ 200100 ｜ 更新: 2026-11-04

## 1. 版本定位

C++11 是一次「大爆炸」：右值引用、移动语义、`auto`、lambda、`nullptr`、`constexpr`、线程库一次性涌入。但 C++11 的很多设计是**半成品**——`constexpr` 函数只能写一条 return、lambda 不能带 `auto` 参数、没有 `make_unique`、没有变量模板。C++14 是一次**补完式小版本**：不引入新范式，只把 C++11 已经开启的方向做完整。

一句话：**C++14 让「现代 C++ 写法」真正顺手**，是 C++11 到 C++17 之间的必经台阶。

## 2. 特性地图

| 特性 | 关键词 | 头文件 | 面试权重 | 本机 |
|---|---|---|---|---|
| 泛型 lambda | `[](auto&&...)` | 语言 | ★★★ | 支持 |
| 函数返回类型推导 | `auto f()` / `decltype(auto)` | 语言 | ★★★ | 支持 |
| 放宽 constexpr | 循环/局部变量/多语句 | 语言 | ★★★ | 支持 |
| 变量模板 | `template<class T> constexpr T pi` | 语言 | ★★ | 支持 |
| `std::make_unique` | 替代 `new` | `<memory>` | ★★★ | 支持 |
| `std::integer_sequence` | 编译期整数序列 | `<utility>` | ★★ | 支持 |
| 初始化捕获 | `[x = std::move(x)]` | 语言 | ★★★ | 支持 |
| 二进制字面量/数字分隔符 | `0b1010'1010` | 语言 | ★ | 支持 |
| `[[deprecated]]` | 弃用标注 | 语言 | ★ | 支持 |
| `std::exchange` | 交换并返回旧值 | `<utility>` | ★★ | 支持 |
| `std::quoted` | 带引号流输出 | `<iomanip>` | ★ | 支持 |
| `shared_timed_mutex` / `shared_lock` | 读写锁 | `<shared_mutex>` | ★★ | 支持 |
| chrono 字面量 | `100ms` / `1s` | `<chrono>` | ★★ | 支持 |
| `_t` 别名模板 | `enable_if_t` / `decay_t` | `<type_traits>` | ★★ | 支持 |

## 3. 重点特性展开

### 3.1 泛型 lambda：lambda 也是模板

C++11 的 lambda 参数必须写具体类型；C++14 允许 `auto` 参数，编译器把闭包的 `operator()` 变成**函数模板**：

```cpp
auto twice = [](auto x) { return x + x; };
auto a = twice(21);        // int
auto b = twice(3.14);      // double

// 万能引用 + 完美转发，写泛型包装器最常用
auto forward_to = [](auto&&... args) {
    target(std::forward<decltype(args)>(args)...);
};
```

要点：

- `auto` 参数让 lambda 成为**泛型闭包**，每个实参类型组合实例化一份 `operator()`。

- `[](auto x)` 按值复制；`[](auto& x)` 只能接左值；`[](auto&& x)` 是万能引用，配合 `std::forward<decltype(x)>(x)` 保持值类别。

- 初始化捕获也是 C++14：`[p = std::move(ptr)]` 把移动后的对象放进闭包，解决了 C++11 无法捕获「只可移动类型」的问题。

### 3.2 返回类型推导：`auto` vs `decltype(auto)`

```cpp
auto add(int a, int b) { return a + b; }          // 推导为 int
auto& ref_to_first(std::vector<int>& v) { return v[0]; } // 显式引用

decltype(auto) get_ref(std::vector<int>& v) { return v[0]; } // 推导为 int&
```

- `auto` 按值推导：**丢弃引用与顶层 const**，`auto x = v[0]` 得到拷贝。

- `decltype(auto)` 保留表达式的引用/const 属性，适合转发函数与容器访问器。

- 所有 `return` 语句必须推导出**同一类型**，否则编译失败；递归函数至少要有一个已声明返回类型的重载或前置声明。

- 返回 `auto` 的函数在**使用点**才实例化返回类型，头文件里定义即可，但跨 TU 的 ODR 与链接仍需注意。

### 3.3 放宽 constexpr：编译期真正能写逻辑

C++11 的 constexpr 函数体只能是一条 `return` 表达式；C++14 允许：

```cpp
constexpr int sum_to(int n) {
    int s = 0;                       // 局部变量
    for (int i = 1; i <= n; ++i) {   // 循环
        s += i;
    }
    return s;                        // 多语句
}
static_assert(sum_to(100) == 5050);
```

仍然禁止：`new`/`delete`（C++20 才放宽）、`try`、`goto`、`asm`、`static`/`thread_local` 变量、虚函数调用（C++20 放宽一部分）。C++14 起 **constexpr 非静态成员函数不再隐式 const**，可以修改对象状态（只要对象生命周期在常量表达式内）。

### 3.4 变量模板：按类型参数化的常量

```cpp
template <typename T>
constexpr T pi = T(3.1415926535897932385);

double area = pi<double> * r * r;
float  farea = pi<float> * rf * rf;
```

- 变量模板是「常量版的函数模板」，常用于类型相关的精度常量、traits 表。

- 变量模板可以定义在头文件中：各 TU 实例化出的特化是 weak/COMDAT 符号，链接器会合并，不会重复定义（本机 `nm -m` 实测为 `weak external`）。

- C++14 真正的缺口在**非模板常量**：namespace 作用域的 `constexpr` 变量是内部链接，每个 TU 一份；`static constexpr` 数据成员被 odr-use 时还需要类外定义。C++17 的 `inline` 变量统一解决这些问题。

### 3.5 `std::make_unique`：终于不用裸 `new`

```cpp
auto p = std::make_unique<int>(42);          // unique_ptr<int>
auto arr = std::make_unique<int[]>(10);      // unique_ptr<int[]>，值初始化
auto obj = std::make_unique<Foo>(1, 2, 3);   // 完美转发到 Foo 构造
```

- C++11 只有 `make_shared`，`make_unique` 是 C++14 补上的；它把 `new` 与所有权绑定在一行，异常安全。

- 不支持自定义删除器：需要 `unique_ptr<T, Deleter>` 时只能显式构造。

- 与 `make_shared` 的区别：`make_shared` 把控制块与对象一次分配；`make_unique` 没有控制块，零额外开销。

### 3.6 `std::integer_sequence`：编译期下标展开

```cpp
template <typename Tuple, std::size_t... I>
auto tuple_sum_impl(const Tuple& t, std::index_sequence<I...>) {
    int values[] = { std::get<I>(t)... };   // 包展开
    int s = 0;
    for (int v : values) s += v;
    return s;
}

template <typename Tuple>
auto tuple_sum(const Tuple& t) {
    return tuple_sum_impl(t, std::make_index_sequence<std::tuple_size<Tuple>::value>{});
}
```

- `std::index_sequence<I...>` 是 `integer_sequence<size_t, I...>` 的别名；常用来把 tuple/参数包展开成数组或逐元素调用。

- C++14 没有折叠表达式（C++17），所以这里用「初始化列表展开 + 循环」而不是 `(std::get<I>(t) + ...)`。

### 3.7 小特性合集：日常代码的高频糖

```cpp
int mask = 0b1010'1010;                       // 二进制 + 数字分隔符
[[deprecated("use new_api()")]] int old_api(); // 弃用标注
int x = 1, y = 2;
int old = std::exchange(x, y);                // old=1, x=2
using namespace std::chrono_literals;
auto timeout = 100ms;                         // chrono 字面量

std::shared_timed_mutex m;
{
    std::shared_lock<std::shared_timed_mutex> lk(m); // 读锁，C++14
    // 多线程只读
}
```

- `_t` 别名：`std::enable_if_t<B, T>`、`std::decay_t<T>`、`std::remove_reference_t<T>` 让 SFINAE 代码短一截。

- `std::quoted` 用于带空格/引号的字符串流输出；`std::cbegin/cend/rbegin/rend` 统一 const 迭代器。

## 4. 本机编译验证

```bash
clang++ -std=c++14 -O2 -Wall example14.cpp -o example14 && ./example14
# 输出：generic=42 pi=3.14159 constexpr=5050 unique=7 tuple=6 capture=42 bin=170 timeout=100 exchange=1
```

覆盖：放宽 constexpr 的 `static_assert`、泛型 lambda、初始化捕获、变量模板、`make_unique`、`index_sequence` 元组求和、二进制字面量、chrono 字面量、`exchange`、`shared_lock`。编译无警告（`-Wall`）。

> 注意：C++14 下不要写折叠表达式。早期探测中 `(std::get<I>(t) + ... + 0)` 在 `-std=c++14` 只报 `-Wc++17-extensions` 警告，Clang 会放行，但这不是合法 C++14；GCC 严格模式会拒绝。版本归属要以标准为准，不以「本机能编过」为准。

## 5. 量化场景落点

- **编译期参数表**：用放宽后的 `constexpr` 生成手续费表、合约乘数表、哈希种子，运行期零初始化成本。

- **泛型回调**：策略/行情回调常用 `[](auto&& tick)` 统一处理不同 tick 类型，避免 `std::function` 的类型擦除与间接调用。

- **所有权**：`make_unique` 管理订单、连接、缓冲区；热路径避免 `shared_ptr` 的原子引用计数。

- **读写锁**：`shared_lock` 保护「读多写少」的配置快照；但热路径仍应优先无锁/原子发布，锁只用于冷路径。

- **时间常量**：`100ms`/`1s` 提高超时、重连、采样周期的可读性，编译期常量不产生运行期开销。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| C++14 相比 C++11 最重要的变化？ | 泛型 lambda、放宽 constexpr、`make_unique`、变量模板、初始化捕获；本质是补完 C++11 |
| `auto` 和 `decltype(auto)` 区别？ | `auto` 按值推导、丢引用与顶层 const；`decltype(auto)` 保留表达式的引用/const |
| 放宽 constexpr 后还有什么不能做？ | 动态分配、`try`、`goto`、`asm`、静态/线程局部变量、虚调用（C++20 部分放宽） |
| 为什么 C++14 没有 `make_unique` 的数组版本？ | 有：`make_unique<T[]>(n)`，返回 `unique_ptr<T[]>`，值初始化 |
| 变量模板放头文件会 ODR 冲突吗？ | 不会：实例化是 weak/COMDAT 符号，链接器合并；C++14 真正的缺口是非模板 `constexpr` 变量与 `static constexpr` 成员的唯一定义 |
| 泛型 lambda 的 `auto&&` 怎么转发？ | `std::forward<decltype(args)>(args)...`，保持左/右值与 const 属性 |

## 7. 参考

- cppreference: [C++14](https://en.cppreference.com/w/cpp/14) / [generic lambda](https://en.cppreference.com/w/cpp/language/lambda) / [constexpr](https://en.cppreference.com/w/cpp/language/constexpr) / [variable template](https://en.cppreference.com/w/cpp/language/variable_template)

- 标准论文：[N3472](https://wg21.link/N3472)（泛型 lambda）、[N3652](https://wg21.link/N3652)（放宽 constexpr）、[N3656](https://wg21.link/N3656)（`make_unique`）

- 书籍：《Effective Modern C++》Item 2/3/28（auto、decltype、完美转发）

- 关联：[move-semantics](../move-semantics/README.md)（初始化捕获与移动）、[smart-pointers](../smart-pointers/README.md)（`make_unique`/`make_shared`）
