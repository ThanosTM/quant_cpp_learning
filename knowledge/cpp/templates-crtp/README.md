# 模板进阶：SFINAE → concepts、CRTP

> 模块: cpp ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-09 ｜ 更新: 2026-11-09
> 依据: [research/03](../../../research/03-tech-stack-map.md)（P1「模板进阶：SFINAE → concepts、CRTP、类型萃取」，HFT 题库 Q12/Q14）

## 1. 一句话定位

模板进阶解决三类问题：**约束**（这个模板能接受什么类型）、**复用**（如何把通用逻辑注入不同类）、**分派**（编译期按类型/能力选实现）。演进路线是 **SFINAE（C++11）→** **`if constexpr`（C++17）→ concepts（C++20）**；CRTP 则是「用继承实现静态多态」的经典手法，在低延迟场景替代虚函数。核心结论：**模板的价值是把运行期开销前移到编译期，代价是编译时间与代码膨胀。**

## 2. 为什么面试官爱问

- **能追到模板实例化机制**：两阶段查找、依赖名称、实例化时机、为什么错误信息一屏。

- **CRTP 是去虚化的标准答案**：撮合/行情热路径用 `static_cast<Derived*>` 静态分派，省掉 vptr 与间接跳转。

- **SFINAE 与 concepts 的对比**：能否讲清「替换失败不是错误」、`enable_if`、`void_t`、`requires` 的四种写法。

- **类型萃取是标准库的地基**：`std::move`/`std::forward`/`std::vector` 的 `allocator_traits` 都靠它。

- 与 [object-model](../object-model/README.md)（vtable）、[modern-cpp](../modern-cpp/cpp20.md)（concepts）互相印证。

## 3. 前置知识

- 函数模板/类模板、显式特化、偏特化。

- 引用折叠与完美转发：[move-semantics §4.5](../move-semantics/README.md)。

- 对象模型与虚函数代价：[object-model](../object-model/README.md)。

- `constexpr` 基础：[constexpr](../constexpr/README.md)。

## 4. 核心讲解

### 4.1 模板实例化：编译期展开的代码

```cpp
template <typename T>
T max(T a, T b) { return a < b ? b : a; }

int x = max(1, 2);          // 实例化 max<int>
double y = max(1.0, 2.0);   // 实例化 max<double>
```

- 每个不同的模板实参组合生成一份**独立代码**（`max<int>`、`max<double>`），这叫模板实例化。

- 两阶段查找：模板定义时检查非依赖名称，实例化时检查依赖名称（`T::foo`）。

- `typename`/`template` 消歧义关键字是依赖名称的必然产物：

```cpp
template <typename T>
void f() {
    typename T::value_type v{};          // 依赖类型要 typename
    T::template rebind<int> r;           // 依赖模板要 template
}
```

- 显式实例化 `template int max<int>(int, int);` 可控制实例化位置、减少重复编译。

### 4.2 CRTP：静态多态

```cpp
template <typename Derived>
struct Shape {
    double area() const { return static_cast<const Derived*>(this)->area_impl(); }
};

struct Circle : Shape<Circle> {
    double r = 1.0;
    double area_impl() const { return 3.14159 * r * r; }
};
```

- 基类模板在编译期知道派生类型，`static_cast` 零成本；调用 `area()` 直接内联到 `Circle::area_impl()`，没有 vptr、没有间接跳转。

- 本机实测：`sizeof(Circle)=8`（只有一个 `double`），空基类优化（EBO）让 `Shape<Circle>` 不占空间；虚函数版本至少多一个 vptr（8 字节）。

- 代价：每个派生类型一份基类实例 → 代码膨胀；不能通过基类指针统一存放（`Shape<Circle>` 与 `Shape<Square>` 是不同类型）。

典型用途：

1. **去虚化**：热路径的策略/回调/访问者。

2. **静态接口 + 混入（mixin）**：`Counted<T>` 给每个派生类独立计数器（本机实验 2：`Order::count=2`、`Trade::count=1`）。

3. **编译期多态链**：`enable_shared_from_this<T>` 就是标准库的 CRTP（见 [smart-pointers §4.5](../smart-pointers/README.md)）。

C++23 的 deducing this 可以替代部分 CRTP（显式对象参数），见 [modern-cpp/cpp23](../modern-cpp/cpp23.md)。

### 4.3 SFINAE：替换失败不是错误

SFINAE = Substitution Failure Is Not An Error：模板实参替换到函数签名失败时，该候选被**静默丢弃**，而不是编译错误。

```cpp
template <typename T>
typename T::value_type first(const T& c) { return *c.begin(); }   // T 无 value_type 时被丢弃
```

常见工具：

```cpp
// enable_if：按条件启用重载
template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
void f(T);

// void_t：检测成员是否存在
template <typename T, typename = void>
struct has_value_type : std::false_type {};
template <typename T>
struct has_value_type<T, std::void_t<typename T::value_type>> : std::true_type {};
```

- `std::void_t<...>` 把任意合法的类型表达式映射为 `void`；表达式非法时偏特化被丢弃。

- `enable_if_t<B, T>` 在 `B=false` 时没有 `type` 成员 → 替换失败 → 候选被丢弃。

- 缺点：错误信息晦涩、重载集合难读、约束不能参与排序。

### 4.4 concepts：约束成为语言特性

```cpp
template <typename T>
concept HasValueType = requires { typename T::value_type; };

template <typename T>
concept Addable = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};

template <HasValueType T>
std::size_t element_hint(const T&) { return sizeof(typename T::value_type); }
```

四种 `requires` 写法：

| 形式                                                  | 用途                 |
| --------------------------------------------------- | ------------------ |
| `requires { typename T::value_type; }`              | 简单要求（类型/表达式合法）     |
| `requires(T a, T b) { a + b; }`                     | 带参数的要求             |
| `requires { { a + b } -> std::convertible_to<T>; }` | 复合要求（表达式 + 返回类型约束） |
| `requires(T t) { t.f(); } && sizeof(T) > 4`         | 嵌套要求/合取            |

优势：

- 错误信息直指「`T` 不满足 `Addable`，因为 `a + b` 不合法」。

- 约束参与**重载排序**（subsumption）：更具体的约束优先。

- 可读性接近普通函数签名；`template <Addable T>` 与 `void f(Addable auto x)` 等价。

- 与 `if constexpr` 分工：concept 决定「能否实例化」，`if constexpr` 决定「实例化后走哪条分支」。

### 4.5 类型萃取：type\_traits 与条件选择

```cpp
std::is_integral_v<T>              // 布尔常量
std::remove_reference_t<T>         // 去引用
std::decay_t<T>                    // 退化为值类型（去引用/const/数组→指针）
std::conditional_t<B, X, Y>        // 编译期三目
std::enable_if_t<B, T>             // 条件启用
std::void_t<...>                   // 检测表达式合法性
```

- 标准库大量使用：`std::move` 用 `remove_reference`，`std::vector` 用 `allocator_traits`，`std::function` 用 `decay`。

- 自己写 trait 时优先组合标准 trait，不要重复造轮子。

- `std::iterator_traits<It>::iterator_category` 是标签分派的基础（见下）。

### 4.6 标签分派与 if constexpr

标签分派（tag dispatch）用**重载决议**在编译期选实现：

```cpp
template <typename It>
void advance_impl(It& it, int n, std::random_access_iterator_tag) { it += n; }

template <typename It>
void advance_impl(It& it, int n, std::input_iterator_tag) { while (n--) ++it; }
```

- 优点：C++11 即可用、错误信息尚可、可针对不同类别写完全不同的实现。

- `if constexpr`（C++17）替代多数场景：函数体内按类型分支，丢弃的分支不实例化。

- 选择：需要不同**函数签名/重载**时用标签分派；只是函数体内分支用 `if constexpr`；需要约束重载集合用 concepts。

### 4.7 可变参数模板与折叠表达式

```cpp
template <typename... Ts>
auto sum_all(Ts... xs) { return (xs + ... + 0); }     // 二元右折叠

template <typename... Ts>
void print_all(Ts&&... xs) { (std::cout << ... << xs); }   // 逗号折叠
```

空包单位元：`&&`→`true`、`||`→`false`、`,`→`void`；`+`/`*` 一元折叠对空包不合法。

`sizeof...(Ts)` 取参数个数；`std::index_sequence` 展开 tuple（见 [modern-cpp/cpp14](../modern-cpp/cpp14.md)）。

### 4.8 模板的工程代价

| 代价   | 说明              | 缓解                            |
| ---- | --------------- | ----------------------------- |
| 编译时间 | 每个实例化组合一份代码     | 显式实例化、extern template、减少组合爆炸  |
| 代码膨胀 | 二进制变大、icache 压力 | 把非依赖部分抽成非模板函数                 |
| 错误信息 | 模板报错层层嵌套        | concepts、`static_assert` 友好提示 |
| 调试困难 | 优化后内联/展开        | `-O0` 调试、`-fno-inline`        |

低延迟取舍：CRTP/模板能去掉虚调用，但会让编译时间与二进制膨胀；**只有热路径**值得这么做，冷路径用虚函数更简单。

## 5. Example 说明

```bash
clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
```

本机实测输出：

```text
=== 1. CRTP 静态多态 ===
circle area=3.142 square area=4.0 sizeof(Circle)=8 sizeof(Square)=8

=== 2. CRTP 计数器 ===
Order::count=2 Trade::count=1
scope 结束后 Order::count=0 Trade::count=0

=== 3. SFINAE 检测成员类型 ===
has_value_type<vector<int>>=1 has_value_type<int>=0

=== 4. concepts 约束 ===
vector<int> value_type 大小=4
Addable<int>=1 Addable<void*>=0

=== 5. 标签分派 ===
advance 后 *it=4

=== 6. 折叠表达式 ===
sum_all(1,2,3,4)=10

=== 7. 模板特化 ===
TypeName<int>=int TypeName<double>=unknown TypeName<vector<int>>=vector<T>

=== 8. if constexpr ===
int=integral double=floating const char*=other
```

观察点：

- `sizeof(Circle)=8` 证明 EBO：空基类 `Shape<Circle>` 不占空间，没有 vptr。

- `Order::count` 与 `Trade::count` 各自独立，是 CRTP 混入的典型效果。

- concepts 的 `static_assert` 在编译期验证约束。

## 6. 高频面试问答

| 问题                            | 30 秒速答                                                                  |
| ----------------------------- | ----------------------------------------------------------------------- |
| CRTP 是什么？                     | 派生类把自己作为模板实参传给基类，基类用 `static_cast<Derived*>` 静态分派，无虚函数开销                |
| CRTP 和虚函数怎么选？                 | 热路径/编译期已知类型用 CRTP；需要运行期多态、插件/异构容器用虚函数                                   |
| SFINAE 是什么？                   | 模板实参替换失败时丢弃该候选而非报错；`enable_if`/`void_t` 是常用工具                           |
| concepts 比 SFINAE 好在哪？        | 可读、错误信息好、约束参与重载排序；写法 `requires` 四种形式                                    |
| `if constexpr` 和 concepts 区别？ | concept 决定模板能否实例化；`if constexpr` 决定实例化后走哪个分支                            |
| 类型萃取常用哪些？                     | `is_integral_v`、`remove_reference_t`、`decay_t`、`conditional_t`、`void_t` |
| 标签分派是什么？                      | 用迭代器类别等标签类型重载，编译期选择实现；`std::advance` 是经典例子                              |
| 模板为什么会让编译变慢？                  | 每个实参组合实例化一份代码；组合爆炸、头文件重复展开                                              |
| 模板代码怎么减少膨胀？                   | 非依赖逻辑抽成普通函数、显式实例化、`extern template`、减少类型组合                              |
| deducing this 能替代 CRTP 吗？     | 能替代部分场景（显式对象参数），但混入/静态计数器等仍常用 CRTP                                      |

## 7. 深度追问记录（落盘区）

| 日期     | 问题     | 结论摘要   | 联动更新   |
| ------ | ------ | ------ | ------ |
| <br /> | <br /> | <br /> | <br /> |

## 8. 参考

- cppreference: [templates](https://en.cppreference.com/w/cpp/language/templates) / [SFINAE](https://en.cppreference.com/w/cpp/language/sfinae) / [constraints](https://en.cppreference.com/w/cpp/language/constraints) / [CRTP](https://en.cppreference.com/w/cpp/language/crtp)（核实日期 2026-11-09）

- 标准论文：[N2634](https://wg21.link/N2634)（SFINAE）、[P0898R3](https://wg21.link/P0898R3)（concepts）、[P0847R7](https://wg21.link/P0847R7)（deducing this）

- 书籍：《C++ Templates: The Complete Guide》第 2 版；《Effective Modern C++》Item 23-30

- 关联：[object-model](../object-model/README.md)（vtable 与去虚化）、[modern-cpp/cpp20](../modern-cpp/cpp20.md)（concepts）、[modern-cpp/cpp23](../modern-cpp/cpp23.md)（deducing this）

