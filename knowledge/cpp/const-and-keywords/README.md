# const 正确性与关键字细节

> 模块: cpp ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-09 ｜ 更新: 2026-11-09
> 依据: [research/03](../../../research/03-tech-stack-map.md)（P1「const 正确性 / 关键字细节」，九坤面经 const 各场景）

## 1. 一句话定位

`const` 不是「只读变量」这么简单：它是**接口契约**（这个函数不修改对象）、**编译期优化的前提**（编译器可假设值不变）、**重载决议的一部分**（const/非 const 成员函数可以重载）。配套关键字 `static`/`inline`/`explicit`/`mutable`/`constexpr`/`volatile` 各自解决一个具体问题，混用或误解就会写出「能编译但语义错」的代码。

## 2. 为什么面试官爱问

- **const 的位置决定语义**：`const int*`、`int* const`、`const int* const` 三者完全不同，面试第一问。

- **const 成员函数能追到 `this` 的类型**：`const` 成员函数里 `this` 是 `const T*`，所以不能改普通成员，但能改 `mutable` 成员。

- **const 正确性影响 API 设计**：成员函数漏写 `const`，`const` 对象就无法调用；返回 `const` 值会抑制移动。

- **关键字细节是区分度**：`static` 在全局/局部/成员三种位置语义不同；`inline` 在 C++17 后能修饰变量；`explicit` 防止隐式转换。

- 直接映射工程：行情快照用 `const&` 传递避免拷贝、配置对象用 `const` 保证只读、热路径用 `constexpr` 把计算前移。

## 3. 前置知识

- 引用与指针、函数重载、类成员函数。

- 移动语义与值类别：[move-semantics](../move-semantics/README.md)（const 返回值为什么抑制移动）。

- 对象布局与 `this` 指针：[object-model](../object-model/README.md)。

- 编译期计算基础：[constexpr](../constexpr/README.md)（本文只做区分，细节见该篇）。

## 4. 核心讲解

### 4.1 const 的位置：从右向左读

```cpp
const int* p1;        // p1 指向 const int：*p1 不可改，p1 可改
int const* p2;        // 同上（const 在 int 右边）
int* const p3;        // p3 是 const 指针：p3 不可改，*p3 可改
const int* const p4;  // 两者都不可改
```

读法：从变量名出发，向右看再向左看。

- `const int*` = 「pointer to const int」；`int* const` = 「const pointer to int」。

- 顶层 const（top-level）：对象本身是 const，如 `int* const`、`const int`。

- 底层 const（low-level）：指向的对象是 const，如 `const int*`、`const int&`。

模板推导只保留**底层 const**：`template<class T> void f(T)` 传入 `const int` 时 `T=int`；传入 `const int*` 时 `T=const int*`。

### 4.2 const 成员函数：`this` 的类型

```cpp
class Buffer {
public:
    int& operator[](std::size_t i) { return data_[i]; }
    const int& operator[](std::size_t i) const { return data_[i]; }
};
```

- 非 const 成员函数里 `this` 的类型是 `Buffer*`；const 成员函数里是 `const Buffer*`。

- 因此 const 成员函数不能修改普通成员，也不能调用非 const 成员函数。

- const/非 const 版本可以**重载**，编译器按对象的 const 属性选择；这是标准容器 `operator[]`/`begin()` 的常见写法。

- 用 `const_cast` 在 const 成员函数里去 const 是危险信号：如果对象本身定义时就是 const，修改它是 UB。

### 4.3 mutable：逻辑 const 的出口

```cpp
int sum() const {
    if (!cache_valid_) {
        sum_ = compute();
        cache_valid_ = true;
    }
    return sum_;
}
mutable int sum_ = 0;
mutable bool cache_valid_ = false;
```

- `mutable` 成员可以在 const 成员函数中修改；语义是「不影响对象**可观察状态**的缓存/统计」。

- 不要用 `mutable` 绕过 const 正确性去改业务数据；那是设计问题，不是语法问题。

- 多线程下 `mutable` 缓存要加锁或原子化，否则 const 方法不再线程安全（`std::shared_mutex` + `mutable` 是常见组合）。

本机实验 2/6 验证：`cref.sum()` 两次调用命中缓存，`counter` 在 const 成员函数里自增成功。

### 4.4 顶层 const 的推导与 auto

```cpp
const int ci = 1;
auto a = ci;             // int：auto 丢弃顶层 const
const auto b = ci;       // const int：显式保留
auto& c = ci;            // const int&：引用保留底层 const
decltype(ci) d = 1;      // const int：decltype 保留完整类型
decltype((ci)) e = ci;   // const int&：带括号的表达式是左值引用
```

- `auto` 按值推导时丢弃顶层 const 与引用；`decltype` 保留。

- 模板参数 `T` 同理：`f(ci)` 推导 `T=int`；`f(std::ref(ci))` 才保留引用语义。

- 泛型代码里如果确实需要 const，用 `std::remove_cvref_t` 显式剥离，或直接用 `auto&&` 万能引用。

### 4.5 static：三种位置三种语义

| 位置 | 语义 | 生命周期 / 链接 |
|---|---|---|
| 全局 `static int x` | 内部链接，仅本 TU 可见 | 程序期 |
| 函数内 `static int x` | 首次执行时初始化，函数间共享 | 程序期 |
| 类内 `static int x` | 属于类而非对象，需类外定义（C++17 `inline static` 可类内定义） | 程序期 |

- 局部静态的初始化是**线程安全**的（C++11 magic statics）；常用于单例：

```cpp
Config& config() {
    static Config instance;   // 首次调用构造，线程安全
    return instance;
}
```

- 类静态成员不占对象空间：本机 `sizeof(Widget)=4`（只有一个 `int id`），`Widget::count` 独立存储。

- `static` 成员函数没有 `this`，不能访问非静态成员。

### 4.6 inline：从「建议内联」到「允许多重定义」

- 函数 `inline`：允许在多个 TU 定义同一函数（链接器合并），**不保证**编译器真的内联。

- C++17 `inline` 变量：头文件中定义全程序唯一的变量，替代 `static const` + 类外定义。

- `constexpr` 静态数据成员在 C++17 起隐式 `inline`。

- 类内定义的成员函数隐式 `inline`。

- 真正的内联决策看优化器（`-O2`、LTO）；`inline` 关键字只解决 ODR。

### 4.7 explicit：阻止隐式转换

```cpp
struct Meters {
    explicit Meters(double v);
};

Meters m{1.5};        // OK：直接初始化
Meters bad = 1.5;     // 错误：explicit 阻止拷贝初始化
void f(Meters);
f(1.5);               // 错误：不能隐式转换
```

- 单参数构造函数默认允许隐式转换，容易写出 `f(42)` 这种意外；`explicit` 关掉它。

- C++11 起转换运算符也可 `explicit`：`explicit operator bool()` 让 `if (obj)` 合法但 `int x = obj` 非法。

- C++20 条件 explicit：`explicit(condition)` 按编译期条件决定是否 explicit。

- 经验：单参构造**默认加 explicit**，除非确实是「值语义转换」（如 `std::string` 从 `const char*`）。

### 4.8 const 返回值与移动

```cpp
const MoveProbe make();          // 返回 const 值
MoveProbe p = std::move(cp);     // cp 是 const：const T&& 绑不上 T&& → 拷贝
```

本机实验 7 输出：

```text
move 非 const：move
move const   ：copy
```

- 返回 `const T` 会阻止调用方的移动构造（`const T&&` 只能绑到 `const T&`）。

- C++17 保证复制消除后，`return T{}` 直接构造到目标，不涉及移动；但显式 `std::move(const_obj)` 仍退化拷贝。

- 结论：**按值返回不要加 const**；函数参数用 `const T&`，需要移动时用 `T&&` 或按值 + `std::move`。

### 4.9 constexpr / consteval / constinit / const 的区别

| 关键字 | 含义 | 求值时机 |
|---|---|---|
| `const` | 运行期只读 | 运行期（也可编译期初始化） |
| `constexpr` | 可以用于常量表达式 | 编译期或运行期 |
| `consteval` | 必须编译期求值 | 仅编译期 |
| `constinit` | 静态初始化必须在编译期完成 | 初始化编译期，变量仍可改 |

```cpp
const int a = runtime_value();      // 运行期初始化，只读
constexpr int b = 42;               // 编译期常量
consteval int square(int n) { return n * n; }
constinit static int counter = 0;   // 无动态初始化，可修改
```

细节见 [constexpr](../constexpr/README.md)。

### 4.10 volatile 不是同步原语

- `volatile` 只告诉编译器「这个值可能被外部改变，不要优化掉读写」，**不提供原子性与内存序**。

- 多线程共享变量用 `std::atomic`，不要用 `volatile`；寄存器映射 IO（MMIO）才需要 `volatile`。

- `volatile` 也不能替代内存屏障；`std::atomic_thread_fence` 才是正确工具（[low-latency](../../low-latency/README.md)）。

### 4.11 final / override：虚函数的编译期检查

```cpp
struct Base { virtual void f(); virtual ~Base() = default; };
struct Derived : Base {
    void f() override;   // 签名不匹配会编译错误
};
struct Leaf final : Derived {};   // 禁止再派生
```

- `override` 让「本想重写但签名写错」变成编译错误；`final` 允许去虚化优化。

- 与对象模型联动：[object-model §4.2](../object-model/README.md)。

## 5. Example 说明

```bash
clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
```

本机实测输出：

```text
=== 1. const 重载 ===
buf[0]=10 cref[0]=10

=== 2. mutable 缓存 ===
sum=19 sum=19

=== 3. 顶层 const 推导 ===
T(ci)=int, T(i)=int（顶层 const 被丢弃）

=== 4. explicit ===
Meters{1.5}.value=1.5，隐式转换被禁止

=== 5. inline static 成员 ===
Widget::count=2 sizeof(Widget)=4

=== 6. const 成员函数 + mutable ===
counter=2 normal=0

=== 7. const 对象抑制移动 ===
move 非 const：move
move const   ：copy
```

覆盖：const 重载、mutable 缓存、顶层 const 推导、`explicit`、`inline static`、const 成员函数、const 对象移动退化。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| `const int*` 和 `int* const` 区别？ | 前者指向 const int（底层 const），后者是 const 指针（顶层 const）；从变量名向右读再向左读 |
| const 成员函数里 `this` 是什么类型？ | `const T*`；不能改普通成员，不能调非 const 成员函数 |
| `mutable` 用来做什么？ | 允许 const 成员函数修改缓存/统计等「逻辑上不改变对象状态」的成员 |
| `auto` 会保留 const 吗？ | 按值推导丢弃顶层 const 与引用；`decltype` 保留完整类型 |
| `static` 三种位置区别？ | 全局：内部链接；局部：首次初始化、线程安全；成员：属于类、需定义 |
| `inline` 是让编译器内联吗？ | 不是保证；它允许函数/变量在多个 TU 定义并由链接器合并 |
| `explicit` 解决什么？ | 阻止单参构造的隐式转换；转换运算符也可 explicit |
| 返回 const 值有什么问题？ | 抑制移动（`const T&&` 只能绑 `const T&`），按值返回不要加 const |
| `constexpr` 和 `const` 区别？ | `constexpr` 可用于常量表达式（编译期），`const` 只是运行期只读 |
| `volatile` 能做线程同步吗？ | 不能；只防优化，不提供原子性与内存序，用 `std::atomic` |

## 7. 深度追问记录（落盘区）

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|

## 8. 参考

- cppreference: [const qualifier](https://en.cppreference.com/w/cpp/language/cv) / [const member functions](https://en.cppreference.com/w/cpp/language/member_functions#const-_and_volatile-qualified_member_functions) / [explicit](https://en.cppreference.com/w/cpp/language/explicit) / [inline](https://en.cppreference.com/w/cpp/language/inline)（核实日期 2026-11-09）

- 书籍：《Effective C++》Item 3（const 正确性）、Item 21（const 成员函数）；《Effective Modern C++》Item 1-4（类型推导）

- 关联：[move-semantics](../move-semantics/README.md)（const 与移动）、[object-model](../object-model/README.md)（this/vptr）、[constexpr](../constexpr/README.md)（编译期计算）
