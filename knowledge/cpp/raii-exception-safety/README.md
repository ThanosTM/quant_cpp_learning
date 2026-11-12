# RAII 与异常安全

> 模块: cpp ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-09 ｜ 更新: 2026-11-09
> 依据: [research/03](../../../research/03-tech-stack-map.md)（P0「RAII 与异常安全」，HFT 题库 Q9/Q18）；关联 [move-semantics](../move-semantics/README.md)、[smart-pointers](../smart-pointers/README.md)

## 1. 一句话定位

RAII（Resource Acquisition Is Initialization）把**资源的生命周期绑定到对象生命周期**：构造时获取，析构时释放。它的价值不只是「不用手写 delete」，而是让**栈展开（stack unwinding）自动清理**成为可能——这正是 C++ 异常安全的基础。异常安全则是对「抛出异常后对象处于什么状态」的分级承诺：不抛（no-throw）、回滚（strong）、有效但可能改变（basic）。

## 2. 为什么面试官爱问

- **一句话区分水平**：「你的析构函数会抛异常吗？」——答会的人没理解 `std::terminate`。

- **能追到编译器的栈展开机制**：异常从抛出点向上传播时，每层栈帧的局部对象按**逆构造顺序**析构；这是 RAII 能工作的前提。

- **直接对应生产 bug**：锁没释放、文件句柄泄漏、构造中途失败导致半初始化对象、`vector` 扩容抛异常后数据损坏。

- **低延迟取舍**：HFT 热路径通常禁用异常，用 `std::expected`/错误码；但 RAII 依然适用于锁、连接、内存池句柄。

- 与 [smart-pointers](../smart-pointers/README.md)、[move-semantics](../move-semantics/README.md) 共同构成「资源管理」考点簇。

## 3. 前置知识

- 构造函数/析构函数、拷贝/移动语义：[move-semantics](../move-semantics/README.md)。

- 智能指针的控制块与所有权：[smart-pointers](../smart-pointers/README.md)。

- 栈与栈帧、函数调用约定（知道「局部对象在栈上」即可）。

- `noexcept` 的基本含义（[move-semantics §4.4](../move-semantics/README.md)）。

## 4. 核心讲解

### 4.1 RAII 的三个动作

```text
构造 ──► 获取资源（new / fopen / lock / mmap / socket）
使用 ──► 正常访问；可能抛异常
析构 ──► 释放资源（delete / fclose / unlock / munmap / close）
```

关键点：

1. **析构函数在栈展开时也会被调用**——这是 RAII 与异常安全的桥梁。

2. 资源所有权唯一：拷贝要禁用或实现深拷贝；移动要转移所有权并把源对象置空（[smart-pointers](../smart-pointers/README.md)）。

3. 析构必须**不抛异常**：栈展开过程中再抛异常 → `std::terminate`。

示例（本机实测）：`raii_demo()` 中先构造 `Tracked` 与 `FileHandle`，随后抛异常；catch 后 `live objects=0`，文件也已关闭——说明异常路径上析构确实执行了。

### 4.2 三种异常保证

| 保证 | 含义 | 典型实现 |
|---|---|---|
| **no-throw**（不抛） | 操作保证不抛异常 | `swap`、`move`、`clear`、析构 |
| **strong**（强保证/提交或回滚） | 失败时对象保持操作前状态 | copy-and-swap、先构造副本再交换 |
| **basic**（基本保证） | 失败时对象仍有效、不泄漏，但状态可能改变 | 大多数容器操作 |

- **no-throw** 是 `vector` 扩容敢用移动的前提；也是 `std::swap` 能作为事务提交手段的原因。

- **strong** 的核心是「所有可能失败的操作都在副本上做，最后用 noexcept 的 swap 提交」。本机实验 2：`push_strong` 在第二次拷贝时抛异常，原对象仍是 `size=2 [1,2]`。

- **basic** 是底线：本机实验 3 中 `push_back` 拷贝抛异常，`vector` 仍有效、`size=2`、无泄漏，但「是否已插入」不确定。

### 4.3 copy-and-swap：强保证的标准写法

```cpp
void push_strong(int v) {
    IntBuffer tmp(*this);        // 1. 拷贝（可能抛）
    tmp.data_.emplace_back(v);   // 2. 修改副本（可能抛）
    data_.swap(tmp.data_);       // 3. noexcept 提交
}                                // 4. tmp 析构，释放旧数据
```

- 第 1、2 步失败时，`*this` 完全没被触碰；第 3 步不会失败。

- `swap` 必须 `noexcept`（标准容器与 `std::swap` 都是）；如果自己写 `swap`，记得标 `noexcept`。

- 代价是一次完整拷贝；大对象可用「先构造新值 + 移动赋值」的变体，但移动赋值本身要能回滚（通常要求 no-throw）。

- 拷贝赋值也可用 copy-and-swap 实现：`T& operator=(T other) { swap(other); return *this; }`（按值传参即拷贝，天然强保证）。

### 4.4 noexcept：契约与代价

`noexcept` 是**承诺**，不是优化提示：

- 函数标了 `noexcept` 却抛异常 → 直接 `std::terminate`，没有栈展开（或实现定义）。

- 析构函数默认隐式 `noexcept(true)`；成员析构可能抛时要显式处理（吞掉/记录日志），不要让它逃出去。

- 移动构造/移动赋值应标 `noexcept`：否则 `vector` 扩容退化为拷贝（[move-semantics §4.4](../move-semantics/README.md)）。

- `noexcept(expr)` 可用于条件声明；`std::is_nothrow_move_constructible_v<T>` 在编译期检测。

本机实验 4：`IntBuffer` 与 `ThrowingCopy` 的 `is_nothrow_move_constructible` 都为 1——`vector` 的移动构造只偷指针，与元素类型无关。

### 4.5 构造失败：析构不会被调用

这是 RAII 最容易踩的坑：

```cpp
class Connection {
public:
    Connection() {
        sock_ = open_socket();      // 1. 获取资源
        throw std::runtime_error("handshake failed");  // 2. 构造失败
    }
    ~Connection() { close_socket(sock_); }  // 不会被调用！
private:
    int sock_;
};
```

- 构造函数抛异常时，该对象的析构函数**不会执行**；但**已构造完成的成员**会按逆序析构。

- 解决：把每个资源交给成员对象管理（`unique_ptr`、`FileHandle`、`SocketGuard`），让成员的析构负责释放；构造失败时成员自动清理。

- 不要在两段式构造（`init()`）里泄漏资源；要么工厂函数返回 `expected`/`unique_ptr`，要么用成员 RAII。

### 4.6 ScopeGuard：把任意清理动作 RAII 化

```cpp
ScopeGuard rollback{[&] { if (!committed) undo(); }};
if (fail) throw ...;        // 异常：rollback 析构，执行 undo
committed = true;           // 成功：手动 dismiss，阻止 undo
rollback.dismiss();
```

- 适合「提交/回滚」事务、临时状态恢复、计时器、日志作用域。

- 析构中执行的回调必须 `noexcept`；否则栈展开时二次异常 → `terminate`。

- C++ 标准库没有 ScopeGuard（`std::experimental::scope_exit` 未进入标准），实际项目常自写或使用 GSL/folly。

- 判断「当前是否在栈展开」用 `std::uncaught_exceptions()`（C++17）：本机实验 6 输出 `uncaught=1, unwinding=yes`。

### 4.7 异常 vs 错误码 vs expected

| 方式 | 成本 | 适用 |
|---|---|---|
| 异常 | 抛出路径昂贵（栈展开、RTTI），正常路径近零 | 构造失败、不可恢复、冷路径 |
| 错误码 | 无隐藏成本，但易被忽略 | C 接口、热路径、跨 ABI |
| `std::optional<T>` | 无错误原因 | 「没有值」而非「失败」 |
| `std::expected<T,E>`（C++23） | 返回值携带错误，零分配（E 轻量时） | 热路径可预期失败 |

- 低延迟项目常编译期关闭异常（`-fno-exceptions`），此时 `new` 失败、标准库部分接口行为要重新评估。

- 团队规则：**同一层不要混用**异常与错误码；边界处（C API、网络协议）统一转换。

## 5. Example 说明

```bash
clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
```

六个实验（本机实测输出）：

```text
=== 1. RAII：异常路径也会析构 ===
caught: boom, live objects=0

=== 2. 强异常保证：copy-and-swap ===
push_strong 抛异常: copy failed, 原对象仍为 size=2 [1,2]

=== 3. 基本保证：资源不泄漏，状态可能已改变 ===
push_back 抛异常: copy failed（vector 仍有效，size=2）

=== 4. noexcept：move 是否可安全用于扩容 ===
IntBuffer move noexcept=1
ThrowingCopy move noexcept=1

=== 5. ScopeGuard：异常回滚 / 正常提交 ===
rollback: 回滚未提交的修改
caught: operation failed
commit: 提交成功

=== 6. 栈展开时的 uncaught_exceptions ===
destructor: uncaught=1, unwinding=yes
caught: unwind
```

观察点：

- 实验 1 的 `live objects=0` 证明异常路径上局部对象正常析构。

- 实验 2 原对象未被污染，实验 3 容器仍可用——两种保证的差别在于「是否回滚」。

- 实验 5 展示 `dismiss()` 如何把「回滚守卫」变成「提交」。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| 什么是 RAII？ | 资源生命周期绑定对象生命周期：构造获取、析构释放；栈展开时析构自动执行 |
| 析构函数能抛异常吗？ | 不能。栈展开中再抛 → `std::terminate`；析构默认 `noexcept` |
| 三种异常保证？ | no-throw 不抛；strong 失败回滚；basic 失败后仍有效不泄漏 |
| copy-and-swap 怎么保证强异常？ | 在副本上做所有可能失败的操作，最后用 noexcept swap 提交 |
| 构造函数抛异常会调用析构吗？ | 不会；但已构造完成的成员会析构。资源交给成员 RAII 管理 |
| `noexcept` 是优化提示吗？ | 不是，是契约；违反直接 terminate。它影响 `vector` 扩容选移动还是拷贝 |
| ScopeGuard 有什么用？ | 把任意清理动作绑定到作用域；适合提交/回滚、计时、临时状态恢复 |
| 如何判断当前在栈展开？ | `std::uncaught_exceptions() > 构造时记录的值` |
| 热路径为什么不用异常？ | 抛出路径有栈展开与 RTTI 成本；错误码/`expected` 更可预测 |
| RAII 和智能指针关系？ | 智能指针是 RAII 的特例：用析构释放堆内存；RAII 还覆盖锁、文件、socket、mmap |

## 7. 深度追问记录（落盘区）

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|

## 8. 参考

- cppreference: [RAII](https://en.cppreference.com/w/cpp/language/raii) / [noexcept](https://en.cppreference.com/w/cpp/language/noexcept_spec) / [uncaught_exceptions](https://en.cppreference.com/w/cpp/error/uncaught_exception) / [exception safety](https://en.cppreference.com/w/cpp/language/exceptions)（核实日期 2026-11-09）

- 标准：[N4618](https://wg21.link/N4618)（异常安全条款）、[P0012R1](https://wg21.link/P0012R1)（`noexcept` 进入类型系统）

- 书籍：《Effective C++》Item 8/25/29；《C++ 标准库》异常安全章节；《Exceptional C++》Item 8-19

- 关联：[move-semantics](../move-semantics/README.md)、[smart-pointers](../smart-pointers/README.md)、[stl-containers](../stl-containers/README.md)（扩容与强保证）
