# 001 构造 / 析构函数中调用虚函数，会发生什么？

> 模块: cpp ｜ 难度: ★★☆ ｜ 频率: 高 ｜ 状态: 已解答 ｜ 收录: 2026-09-02
> 来源: 富途证券 C++ 面经、九坤量化面经原题摘录（链接清单见 [research/04](../../research/04-interview-landscape.md)）

## 题目

在基类构造函数中调用虚函数，调用的是基类版本还是派生类重写版本？析构函数中呢？如果调用的是纯虚函数会怎样？

## 一句话答案

**调用的是「当前正在构造 / 析构的那一层」的版本**：构造基类时 vptr 指向基类 vtable，虚调用解析到基类实现（即使派生类重写了它）；析构时同理，派生层先被销毁，虚调用回落到当前层。若调用的是纯虚函数，标准规定是未定义行为，glibc 环境通常打印 `pure virtual method called` 并 `abort()`。

## 详细解答

### 1. vptr 是逐层设置的

构造顺序：基类子对象 → 基类构造体 → 成员 → 派生类构造体。**进入基类构造函数时，vptr 先被编译器置为基类的 vtable**；执行完基类构造、进入派生类构造函数时，vptr 才被改写为派生类 vtable。

所以基类构造函数中的虚调用，查的是基类 vtable → 解析到基类版本。这不是「多态失效了」，而是**此刻对象的动态类型就是基类**——派生类部分还不存在，成员都没初始化。

```
构造期间 vptr 的变化：
Base::Base()  进入 → vptr ──→ Base vtable   ← 此时虚调用只会落在 Base 层
成员依次构造 ↓
Derived::Derived() 进入 → vptr ──→ Derived vtable
```

### 2. 析构是镜像过程

析构顺序相反：`~Derived()` 先执行完（此时对象动态类型已回退到 Base），随后进入 `~Base()` 时 vptr 被置回基类 vtable。因此**析构函数中的虚调用同样解析到当前层版本**，派生类重写版本已不可达。

### 3. 纯虚函数的特殊情况

- 构造 / 析构期间调用纯虚函数：[CppReference](https://en.cppreference.com/w/cpp/language/virtual#During_construction_and_destruction) 明确这是未定义行为（UB）。

- 实际表现：类有纯虚函数却**没有给出定义**时，vtable 对应槽位无有效地址；glibc 下运行时报 `pure virtual method called` 后 `abort()`，是最常见的崩溃现场。

- 若纯虚函数**有定义**（`virtual void f() = 0;` 且类外提供了实现），构造期调用到它会正常执行该定义——这也是「给纯虚函数提供默认实现」这一技巧能成立的原因。

### 4. 为什么语言这么设计

如果允许下沉到派生类重写版本，该版本可能访问**尚未构造 / 已销毁**的派生类成员，等于鼓励未定义行为。C++ 采取「构造/析构期间动态类型 = 当前层」的规则，把危险挡在语言层面。对比：**Java / C# 中基类构造函数会调用派生类重写**（虚分派始终生效），因此 Java 规范明确警告不要在构造函数中调用可重写方法——同一问题，两种语言的取舍不同，面试可作加分对比点。

### 5. 正确姿势

想在构造期获得「派生行为」：两段式初始化（先构造再 `init()`）、工厂函数返回 `unique_ptr<Base>` 后调用初始化接口、或用 CRTP 静态多态在编译期绑定派生实现。

## 代码验证

```cpp
#include <iostream>
struct Base {
    Base() { hook(); }              // 调用的是 Base::hook，不是 Derived::hook
    virtual ~Base() = default;
    virtual void hook() const { std::cout << "Base::hook\n"; }
};
struct Derived : Base {
    void hook() const override { std::cout << "Derived::hook\n"; }
};
int main() { Derived d; }           // 输出: Base::hook
```

把 `hook()` 改为 `= 0` 且不提供定义，glibc 下运行即得 `pure virtual method called`（编译仍通过，问题出在运行期）。

## 追问链（面试常见延伸）

1. 为什么基类指针析构要 `virtual ~Base()`？→ 否则 `delete base_ptr` 是 UB；基类有虚函数就该有虚析构（关联：[cpp/object-model](../../knowledge/cpp/object-model/README.md)）。
2. Java 在构造函数中调虚函数会发生什么？→ 会调用派生类版本（见 §4），所以 Java 强调构造期不可重写方法。
3. vptr 什么时候被设置？编译器插入了什么代码？→ 每层构造/析构开头由编译器写入该层 vtable 地址（关联：[cpp/object-model](../../knowledge/cpp/object-model/README.md)）。
4. 怎么在构造期实现多态？→ 两段式初始化 / 工厂 + init / CRTP。
5. 顺带：构造函数可以是 virtual 吗？→ 不可以，虚机制依赖 vptr，而 vptr 要构造完成后才指向派生层。

## 关联知识点

- [cpp/object-model](../../knowledge/cpp/object-model/README.md)（vtable/vptr、对象内存布局、多重继承、RTTI）

## 落盘记录

| 日期         | 事件   | 结论          |
| ---------- | ---- | ----------- |
| 2026-09-02 | 首次解答 | 正文完成，代码验证通过 |

