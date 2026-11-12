# constexpr 编译期计算

> 模块: cpp ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-09 ｜ 更新: 2026-11-09
> 依据: [research/03](../../../research/03-tech-stack-map.md)（P1「constexpr 编译期计算」，HFT 题库 Q11）

## 1. 一句话定位

`constexpr` 让函数和变量**可以在编译期求值**：同一个函数既能用于 `static_assert`、数组大小、模板实参等常量表达式上下文，也能在运行期以普通函数身份执行。C++20 引入 `consteval`（强制编译期）与 `constinit`（强制静态初始化），把「编译期」这件事从「可以」细化为「必须」与「只限初始化」。核心价值：**把计算从运行期搬到编译期，热路径只读常量。**

## 2. 为什么面试官爱问

- **能区分 `const` / `constexpr` / `consteval` / `constinit`**：四个关键字语义完全不同，混答立刻露馅。

- **能追到编译器实现**：常量表达式求值器、递归深度限制、`-fconstexpr-steps`、为什么 `constexpr` 函数里不能有 `reinterpret_cast`。

- **量化场景直接受益**：手续费表、合约乘数、协议字段哈希、单位换算都可在编译期生成，运行期零成本。

- **与模板/类型萃取联动**：`if constexpr` 是模板分支的基础；`constexpr` 变量模板是类型相关常量的标准写法。

- HFT 题库 Q11 明确考「编译期计算与运行期计算的区别」。

## 3. 前置知识

- `const` 与只读语义：[const-and-keywords](../const-and-keywords/README.md)。

- 模板与 `if constexpr` 基础：[templates-crtp](../templates-crtp/README.md)。

- 编译/链接流程（预处理、编译、汇编、链接）——知道「编译期」发生在哪一步。

- C++14 放宽 constexpr 的语法（[modern-cpp/cpp14](../modern-cpp/cpp14.md) §3.3）。

## 4. 核心讲解

### 4.1 常量表达式与可用上下文

常量表达式（constant expression）是**编译器能在编译期算出结果**的表达式。只有常量表达式才能出现在这些位置：

```cpp
constexpr int n = 10;
int arr[n];                              // 数组大小
static_assert(n == 10);                  // 静态断言
template <int N> struct S {};
S<n> s;                                  // 模板实参
constexpr int m = n * 2;                 // constexpr 变量初始化
case n: break;                           // case 标签
```

`const int` 不一定是常量表达式：

```cpp
int runtime();
const int a = runtime();     // 运行期初始化，不是常量表达式
constexpr int b = 42;        // 常量表达式
```

### 4.2 constexpr 的演进

| 标准 | 能力 |
|---|---|
| C++11 | constexpr 函数体只能是一条 `return` 表达式；字面类型限制多 |
| C++14 | 允许局部变量、循环、多语句、`if`/`switch`（本机 `factorial` 示例） |
| C++17 | `if constexpr`、constexpr lambda、`constexpr` 静态成员隐式 inline |
| C++20 | `consteval`、`constinit`、`constexpr new/delete`、`constexpr std::vector/std::string`、虚函数 constexpr |
| C++23 | `if consteval`（替代 `is_constant_evaluated`） |
| C++26 | constexpr 异常、constexpr 虚继承、constexpr `void*` 转换 |

C++20 起 constexpr 函数里可以动态分配，但**分配必须在常量表达式求值结束前释放**：

```cpp
constexpr int sum_vector() {
    std::vector<int> v{1, 2, 3, 4};   // C++20：常量求值中可以 new
    int s = 0;
    for (int x : v) s += x;
    return s;
}
static_assert(sum_vector() == 10);
```

仍然禁止：`reinterpret_cast`（C++26 放宽部分）、`asm`、`goto`、`try`（C++26 放宽）、静态/线程局部变量、未定义行为。

### 4.3 同一函数的两种执行方式

```cpp
constexpr int factorial(int n) {
    int r = 1;
    for (int i = 2; i <= n; ++i) r *= i;
    return r;
}

constexpr int a = factorial(5);   // 编译期求值，a 是常量
int n = 6;
int b = factorial(n);             // 运行期求值，普通函数调用
```

本机实测：`factorial(5)=120`（编译期）、`factorial(6)=720`（运行期）。

- 编译器不保证「能编译期算就一定编译期算」；只有出现在常量表达式上下文才**必须**编译期求值。

- 同一个 `constexpr` 函数可能生成两份代码（编译期求值器 + 运行期函数体），二进制会变大。

- `constexpr` 函数隐含 `inline`，头文件中定义不会 ODR 冲突。

### 4.4 consteval：强制编译期

```cpp
consteval int square(int n) { return n * n; }

constexpr int a = square(7);   // OK
int x = 7;
int b = square(x);             // 编译错误：x 不是常量表达式
```

- `consteval` 函数（立即函数）每次调用都必须在编译期求值，否则编译失败。

- 适合「只允许编译期」的 API：编译期哈希、类型 ID、单位换算、维度检查。

- 与 `constexpr` 的区别：`constexpr` 是「可以编译期」，`consteval` 是「必须编译期」。

### 4.5 constinit：消灭静态初始化顺序问题

```cpp
constinit int g_counter = 100;   // 静态初始化阶段完成
g_counter += 1;                  // 仍然可以修改
```

- `constinit` 只约束**初始化**在编译期完成（无动态初始化），变量本身不是 const。

- 解决「静态初始化顺序问题」（static initialization order fiasco）：跨 TU 的全局对象初始化顺序未定义，动态初始化可能读到未初始化值。

- `constinit` 不能用于局部变量/成员变量；只能用于静态存储期变量。

- 与 `constexpr` 区别：`constexpr` 隐含 const 且必须常量初始化；`constinit` 不要求 const，只要求常量初始化。

### 4.6 if consteval 与 is_constant_evaluated

C++20 的写法（`std::is_constant_evaluated()`）容易踩坑：

```cpp
constexpr int f(int n) {
    if (std::is_constant_evaluated()) {
        return n * 2;      // 编译期
    }
    return n + 1;          // 运行期
}
```

- 本机 C++20 实测：`compile=20`（常量上下文走 ×2）、`runtime=11`（运行期走 +1）。

- 坑：`if (std::is_constant_evaluated())` 在运行期是普通 `if`，编译器可能把它优化掉或误判；必须直接用在 `if` 条件里，不能存到变量再判断。

- C++23 的 `if consteval { ... } else { ... }` 是语法级分支，语义清晰、不会被优化误导。**注意**：`if consteval` 是 C++23 特性，`-std=c++20` 下 Clang 只作为扩展并给警告。

### 4.7 编译期字符串哈希与查找表

```cpp
constexpr std::uint32_t fnv1a(const char* s, std::size_t n) {
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= static_cast<unsigned char>(s[i]);
        h *= 16777619u;
    }
    return h;
}

template <std::size_t N>
constexpr std::uint32_t hash_literal(const char (&s)[N]) {
    return fnv1a(s, N - 1);
}

static_assert(hash_literal("order") == fnv1a("order", 5));
```

- 编译期算好字段哈希，运行期协议解析用 `switch`/数组下标，避免字符串比较。

- 本机实测：`hash("order")=1932267671`、`hash("trade")=1362521689`（FNV-1a 32 位）。

- 同理可生成：手续费阶梯表、合约乘数表、Greeks 查找表、CRC 表。运行期只读 `.rodata`，无初始化成本。

### 4.8 编译期成本与限制

| 限制 | 说明 |
|---|---|
| 递归/循环步数 | 编译器有上限（Clang `-fconstexpr-steps`，默认约 1e6） |
| 编译时间 | 复杂 constexpr 求值会显著拖慢编译 |
| 代码膨胀 | 每个实例化组合一份；编译期求值器与运行期代码可能共存 |
| 二进制大小 | 编译期生成的数组进入 `.rodata` |
| 可调试性 | 编译期计算无法断点；错误信息指向 `static_assert` |

工程建议：

1. 编译期只做**确定、有限、可缓存**的计算；不要把所有逻辑都 constexpr 化。

2. 大表优先用代码生成（脚本）而不是 constexpr 递归，编译时间更可控。

3. 用 `static_assert` 固化关键不变量，让错误在编译期暴露。

## 5. Example 说明

```bash
clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
```

本机实测输出：

```text
=== 1. constexpr 函数 ===
factorial(5)=120 factorial(6)=720

=== 2. 编译期数组 ===
kSquares[0..9]=0 1 4 9 16 25 36 49 64 81

=== 3. consteval ===
square(9)=81

=== 4. constinit ===
g_counter=100
after +=1: 101

=== 5. is_constant_evaluated ===
compile=20 runtime=11

=== 6. 编译期字符串哈希 ===
hash(order)=1932267671 hash(trade)=1362521689

=== 7. constexpr 变量模板 / lambda ===
kPi<double>=3.14159 kDouble(21)=42

=== 8. 类型属性 ===
factorial 可常量求值=1 kSquares 是 const=1
```

覆盖：constexpr 循环、编译期数组生成、consteval、constinit 可修改性、`is_constant_evaluated` 双路径、FNV-1a 编译期哈希、变量模板、constexpr lambda。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| `const` 和 `constexpr` 区别？ | `const` 是运行期只读；`constexpr` 表示可用于常量表达式（编译期求值） |
| `consteval` 和 `constexpr` 区别？ | `consteval` 必须编译期求值；`constexpr` 允许编译期或运行期 |
| `constinit` 是什么？ | 强制静态初始化在编译期完成，变量仍可修改；解决静态初始化顺序问题 |
| constexpr 函数一定在编译期执行吗？ | 不一定；只有常量表达式上下文才强制编译期，普通调用走运行期 |
| C++20 constexpr 能动态分配吗？ | 能，但必须在常量求值结束前释放；`constexpr std::vector` 可用 |
| `if consteval` 是哪个标准？ | C++23；C++20 用 `std::is_constant_evaluated()` |
| `is_constant_evaluated` 有什么坑？ | 必须直接用于 `if` 条件；存变量后再判断会失去常量求值语义 |
| 编译期哈希有什么用？ | 协议字段/命令名编译期算好，运行期 `switch`/查表，避免字符串比较 |
| constexpr 有什么代价？ | 编译时间、代码膨胀、递归步数限制、调试困难 |
| 什么时候不该用 constexpr？ | 大表/复杂计算拖慢编译、需要运行期输入、逻辑频繁变动 |

## 7. 深度追问记录（落盘区）

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|

## 8. 参考

- cppreference: [constexpr](https://en.cppreference.com/w/cpp/language/constexpr) / [consteval](https://en.cppreference.com/w/cpp/language/consteval) / [constinit](https://en.cppreference.com/w/cpp/language/constinit) / [is_constant_evaluated](https://en.cppreference.com/w/cpp/language/constant_expression)（核实日期 2026-11-09）

- 标准论文：[N3652](https://wg21.link/N3652)（C++14 放宽 constexpr）、[P0859R0](https://wg21.link/P0859R0)（constexpr new）、[P1938R3](https://wg21.link/P1938R3)（`if consteval`）、[P1143R2](https://wg21.link/P1143R2)（`constinit`）

- 书籍：《C++17 完全指南》第 6 章；《C++20 完全指南》constexpr/consteval 章节

- 关联：[const-and-keywords](../const-and-keywords/README.md)、[templates-crtp](../templates-crtp/README.md)、[modern-cpp/cpp14](../modern-cpp/cpp14.md)、[modern-cpp/cpp23](../modern-cpp/cpp23.md)
