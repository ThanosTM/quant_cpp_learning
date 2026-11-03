# 对象模型：vtable / vptr、多重继承与 RTTI

> 模块: cpp ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-03 ｜ 更新: 2026-11-03
> 依据: [research/03](../../../research/03-tech-stack-map.md)（P0「对象模型：vtable/vptr、多重继承内存布局、RTTI」；九坤、富途真题）；关联真题 [interview/cpp/001](../../../interview/cpp/001-constructor-virtual-function.md)

## 1. 一句话定位

C++ 对象模型回答两个问题：**对象在内存里长什么样**（数据成员、对齐、vptr），以及**虚调用与类型转换在运行期如何找到正确的函数和子对象地址**。核心结论：多态对象每个（非虚）基类子对象各带一个 vptr；vtable 是编译期生成的静态表（单继承下一个多态类一份，多继承下每个多态基类子对象对应一份）；多重继承下不同基类指针指向对象内的不同偏移，`static_cast` / `dynamic_cast` 会调整 `this`，`reinterpret_cast` 不会。

## 2. 为什么面试官爱问

- **一行虚调用能问到 ABI 层**：`p->f()` 背后是「取 vptr → 取槽 → 间接跳转」，能继续追问 vtable 里 `-1` / `-2` 槽、析构器为什么成对出现、槽位顺序与 ABI 稳定性。

- **构造 / 析构中调虚函数**是同一考点簇：只有理解 vptr 在每层构造/析构时被改写，才能解释为什么调到当前层版本（真题 [001](../../../interview/cpp/001-constructor-virtual-function.md)）。

- **多重继承是分水岭**：`Base2*` 与 `Derived*` 地址不同、虚调用要经 thunk 调整 `this`、对象切片与指针比较陷阱——背八股的人在这里露馅。

- **直接映射到量化工程**：热路径去虚化（`final` / LTO / CRTP）、对象池与序列化要求 standard-layout / trivially copyable、跨动态库传递多态对象的 ABI 风险。

- 智能指针的 EBO（空基类优化）实现、缓存行布局与伪共享都以对象布局为前提，可与 [smart-pointers](../smart-pointers/README.md)、[false-sharing](../../low-latency/false-sharing/README.md) 联动追问。

## 3. 前置知识

- C++ 类、继承、虚函数基础（`override` / `final` / 纯虚函数）——无独立知识点，默认已掌握。

- 指针与引用的四种转换（`static_cast` / `dynamic_cast` / `reinterpret_cast` / `const_cast`）。

- 建议先读真题 [interview/cpp/001](../../../interview/cpp/001-constructor-virtual-function.md)，本文会从 vptr 角度回填它的底层机制。

## 4. 核心讲解

### 4.1 对象内存布局：数据成员 + 对齐 + vptr

一个对象占用的空间由三部分组成：

1. **非静态数据成员**：按声明顺序排列（同一访问控制段内），受对齐（alignment）约束插入 padding；`static` 成员、成员函数不占对象空间。
2. **对齐填充**：对象大小是 `alignof` 的整数倍，便于数组连续访问。
3. **vptr**：类只要（或继承）有虚函数，就是多态类型（polymorphic type），编译器会在每个多态子对象里放入 vptr。位置由 ABI 决定，Itanium ABI 放在**对象起始处**。

实测（64 位，Apple clang / Itanium ABI，见 §5）：

| 类型 | `sizeof` | 组成 |
|---|---:|---|
| `struct Empty {}` | 1 | 标准要求不同对象地址不同，空类至少 1 字节 |
| `struct Plain { int a; char c; }` | 8 | 4 + 1 + 3 padding |
| `struct Polymorphic { virtual ~Polymorphic(); int a; }` | 16 | vptr 8 + int 4 + 4 padding |
| `struct WithEbo : Empty { int x; }` | 4 | 空基类优化（EBO），空基类不占存储 |
| `struct WithoutEbo { Empty e; int x; }` | 8 | 空类作为**成员**时仍占 1 字节 + padding |
| `struct Multi : Base1, Base2 { int z; }` | 32 | 两个 vptr + 两份基类数据 + `z` 复用 `Base2` 的尾部 padding |

两个容易答错的细节：

- **尾部 padding 复用**：派生类成员可以放进基类的尾部 padding（Itanium ABI），所以 `sizeof(Derived)` 不一定等于 `sizeof(Base) + sizeof(member)`。`Multi` 的两个基类各占 16 字节，`z` 放进 `Base2` 的 padding，总大小仍是 32 而不是 40。

- **standard-layout**：无虚函数、无虚基类、所有非静态成员同一访问控制、至多一个带非静态数据成员的基类，且基类也满足这些条件。`Plain` 是 standard-layout，`Polymorphic` 不是——后者不能安全地按字节序列化/`memcpy` 后跨进程解释；即使 standard-layout，bitwise 拷贝还要求 trivially copyable（无虚函数的多态类不满足）。

- C++20 的 `[[no_unique_address]]` 可以把「空成员」压到 0 字节，是成员位置的 EBO 等价物（[smart-pointers §4.2](../smart-pointers/README.md) 已用它解释 `unique_ptr` 的零开销删除器）。

### 4.2 vptr 与 vtable（Itanium ABI）

**vptr 是对象里的一个隐藏指针，vtable 是编译期生成的只读表**（单继承下一个多态类一份；多继承下每个多态基类子对象各对应一份，见 §4.3）。vptr 指向 vtable 的「地址点」——第一个虚函数槽，而不是表的物理开头；地址点前面还有两个固定槽：

```
对象 b（B : A）                      B 的 vtable（每个类一份，位于只读数据段）
┌────────────────┐                ┌─────────────────────────────────────┐
│ [0] vptr ──────┼───────────────→│ [-2] offset-to-top = 0              │
│ [8] 数据成员…   │                │ [-1] &type_info(B)                  │
└────────────────┘                │ [0]  D1 complete object destructor   │
                                  │ [1]  D0 deleting destructor          │
                                  │ [2]  &B::f                           │
                                  │ [3]  &A::g   ← 未重写，复用基类槽     │
                                  └─────────────────────────────────────┘
```

- `[-2]` **offset-to-top**：从当前子对象地址回到完整对象地址的偏移，单继承主基类为 0，第二基类为负值（§4.3）。

- `[-1]` **type_info 指针**：RTTI 的入口，`typeid(*p)` 与 `dynamic_cast` 都依赖它（§4.5）。

- 虚析构会展开成 **D1（complete object destructor，只析构）** 与 **D0（deleting destructor，析构后 `delete`）** 两个槽。本例析构函数声明在最前，因此占 `[0]` / `[1]`；这是 GCC/Itanium 的实现细节，面试时说清「析构器成对、位置随声明/ABI 而定」即可。

- **槽位按声明顺序固定**：在类中间插入一个新虚函数会平移后续槽位，破坏二进制兼容（ABI）。跨 `.so` / 插件边界传多态对象时，这是实打实的风险。

虚调用在机器层只有三步（示例中 `f` 在第 2 槽）：

```asm
mov rax, [rdi]        ; 1. 取对象首部的 vptr
call [rax + 16]       ; 2. 按静态类型已知的槽偏移取函数地址
                      ; 3. 间接跳转（分支预测器的目标）
```

编译器能**去虚化**（devirtualization）时会把这三步优化成直接调用：`final` 类/函数、匿名命名空间内可见的类、LTO 可见完整继承图、或局部已知动态类型。热路径上「虚函数一定慢」不成立，但**多态调用点（megamorphic call site）** 会让 BTB 预测失败，这是低延迟场景真正要避免的。

构造 / 析构期间的 vptr 是逐层改写的：进入 `Base::Base()` 时 vptr 指向 Base vtable，进入 `Derived::Derived()` 才改写为 Derived vtable；析构镜像。这正是真题 001 的底层解释。也解释了**基类析构必须 virtual**：`delete base_ptr` 要通过 vtable 找到派生类析构，否则是 UB，派生类成员不会析构。

### 4.3 多重继承：地址调整与 thunk

非虚多重继承下，每个多态基类子对象都带自己的 vptr；第二个基类子对象不在偏移 0：

```
Multi 对象（sizeof = 32）
偏移 0   ┌──────────────────┐
         │ Base1 vptr       │ → Base1 vtable（offset-to-top = 0）
偏移 8   │ Base1::x         │
偏移 16  ├──────────────────┤
         │ Base2 vptr       │ → Base2 vtable（offset-to-top = -16）
偏移 24  │ Base2::y         │
偏移 28  │ Multi::z         │ ← 复用 Base2 的尾部 padding
         └──────────────────┘
```

由此产生四个必须掌握的结论：

1. **指针值不同**：`static_cast<Base2*>(&m)` 会把地址 +16；`reinterpret_cast<Base2*>(&m)` 不调整，得到错误地址。把 `void*` 原样转回 `Base2*` 同样丢失调整——这是实际代码里的经典 bug。

2. **虚调用经 thunk**：`p2->id2()` 先通过 Base2 vtable 找到 thunk，thunk 把 `this` 减 16 调回完整对象，再跳 `Multi::id2`。所以 `p2->id2()` 能正确返回派生类结果（示例返回 22）。

3. **`offset-to-top` 用于找回完整对象**：`dynamic_cast<void*>(p1)` 与 `dynamic_cast<void*>(p2)` 都返回 `&m`；而 `static_cast<void*>` 保留的是各自子对象地址，两者不相等。**比较两个基类指针是否指向同一对象，不能直接比地址**。

4. **跨基类转换要 `dynamic_cast`**：`dynamic_cast<Base1*>(p2)` 会反向调整回偏移 0；`static_cast` 在编译期只知道「向下/向上」的固定偏移，跨兄弟基类无法完成。

另外两个陷阱：

- **对象切片（object slicing）**：按值把 `Derived` 传给 `Base` 形参/赋值，只拷贝 Base 子对象，vptr 变成 Base vtable，派生成员丢失，多态行为消失。接口参数用 `const Base&` 或指针。

- **空基类不是无条件 0 字节**：单个空基类可被 EBO 吃掉；多个空基类、或空类作为成员时仍可能占 1 字节（示例 `WithoutEbo` 为 8）。

### 4.4 虚继承：共享唯一基类子对象

菱形继承 `Diamond : Left, Right`，两者都 `virtual` 继承 `VBase` 时，`VBase` 子对象只存一份：

```
        VBase（唯一子对象）
        ↑                ↑
      Left            Right
        ↑                ↑
          Diamond
```

- 访问虚基类的位置不是编译期常量：同一个 `Left` 单独存在时 `VBase` 在偏移 16，作为 `Diamond` 的基类时在偏移 32，所以 `sizeof(Left)` / `sizeof(Right)` 明显变大（实测各 32 字节，`Diamond` 48 字节）。

- **虚基偏移存在 vtable 的负槽里，没有独立的 `vbptr`**：Itanium ABI 从 `[-3]` 起为每个虚基类存一项（到虚基子对象的偏移 + 到其 vptr 的偏移），`vptr` 兼任 vbptr，访问虚基成员因此多一次依赖加载；独立 `vbptr` + `vbtable` 是 MSVC 的做法。VTT 只在构造/析构期使用：虚基类由最派生类构造，中间层用构造 vtable 临时把 `VBase` 当成偏移 16。

- `static_cast<Left*>(&dia)` 与 `static_cast<Right*>(&dia)` 得到的 `VBase*` **地址相同**（示例验证），因为共享同一子对象。

- 构造顺序：**虚基类最先**，然后按声明顺序构造直接基类，再构造成员，最后执行派生类构造体；析构完全逆序。虚基类由最派生类负责构造，中间层不会重复构造它。

- 代价：额外间接寻址、对象更大、ABI 更复杂、不能是 standard-layout。工程上优先「非虚继承 + 组合」；虚继承主要用于接口/菱形语义，热路径慎用。

### 4.5 RTTI：typeid 与 dynamic_cast

RTTI 的信息挂在 vtable 的 `[-1]` 槽（`type_info` 指针）：

- `typeid(T)`：编译期得到静态类型的信息；`typeid(*p)`：当 `*p` 是多态类型时**运行期**查 vptr 的 `[-1]` 槽，得到动态类型。对非多态类型 `typeid(*p)` 仍是静态的；`typeid` 会去掉 cv 限定与引用。

- `dynamic_cast<T*>(p)`：失败返回 `nullptr`；`dynamic_cast<T&>(p)`：失败抛 `std::bad_cast`（示例中 `Cat&` 分支）。它要求源类型是多态的（有虚函数），否则编译失败。

- 实现上要沿继承图检查目标类型并计算指针调整，成本远高于一次虚调用，且随继承深度/宽度增长；**热路径避免 `dynamic_cast`**，用标签枚举、`std::variant` + `visit`、或访问者模式替代。

- `dynamic_cast<void*>(p)` 是特例：返回**完整对象**（most-derived object）的地址，用于判断两个基类指针是否同一对象（§4.3）。

- `-fno-rtti` 会移除 `typeid` / `dynamic_cast` 支持（vtable 的 `[-1]` 槽不再可用），一些低延迟/嵌入构建会关闭 RTTI；异常机制本身不依赖你手写 RTTI 查询，但跨 DSO 的异常类型匹配仍需注意 type_info 合并。

- `typeid(*p)` 的 `.name()` 是**编译器修饰名**（Itanium ABI 会 mangle，如 `N12_GLOBAL__N_13DogE`），不是可读类型名，不要用于展示或比较字符串；比较用 `typeid` 的 `==`。

### 4.6 工程权衡与量化场景

| 场景 | 建议 | 原因 |
|---|---|---|
| 热路径多态 | `final` / CRTP / 模板 / `std::variant` | 去虚化或消除间接跳转与 BTB 压力 |
| 业务层接口 | 虚函数 + 虚析构 | 可扩展、可测试，开销可接受 |
| 对象池 / 序列化 | standard-layout + trivially copyable | 可按字节搬运；多态对象带 vptr，不可直接序列化 |
| 跨 DSO / 插件 | C ABI、pImpl、opaque handle | vtable 槽位与 type_info 跨编译器不稳定 |
| 指针身份比较 | `dynamic_cast<void*>` | 多继承下子对象地址不同 |
| 频繁 `dynamic_cast` | 重构为标签/访问者 | RTTI 遍历继承图，成本高 |

- **对象大小与缓存**：每个多态基类多 8 字节 vptr；对象数组里 vptr 与数据混排会降低缓存密度。低延迟数据结构常把「热字段」与「冷字段/vptr」分离（见 [false-sharing](../../low-latency/false-sharing/README.md) 的缓存行视角）。

- **测而不是猜**：虚调用是否被去虚化，直接看汇编（`clang++ -S` / `objdump -d`）或看 profile 的间接分支 miss；不同编译器/优化级别结论可能相反。

- **ABI 是隐式契约**：同一份头文件用不同编译器/标准库编译出的 vtable 布局、RTTI 结构可能不兼容；跨边界传多态对象是事故高发区。

## 5. Example 说明

```bash
g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
```

- 实验 1：`sizeof(Empty)=1`、`Plain=8`、`Polymorphic=16`、`WithEbo=4` vs `WithoutEbo=8`，并静态断言 EBO 与 standard-layout 判定。

- 实验 2：读取对象首部的 vptr，打印 `vtable[-1]` 的 RTTI 名与 `[0..3]` 槽；`B` 只重写 `f`，因此 `[2]` 槽不同、`[3]` 槽与 `A` 相同；手工按 `vtable[2]` 调用返回 2，与普通 `b.f()` 一致。

- 实验 3：`sizeof(Multi)=32`；`p2` 相对 `&m` 偏移 +16；`offset-to-top(Base2)=-16`；`void*` 中转后转回 `Base2*` 得到 `&m`（错误地址）；`dynamic_cast<Base1*>(p2)` 与 `dynamic_cast<void*>` 正确回到完整对象。

- 实验 4：`VBase` 经 `Left` / `Right` 两条路径地址相同；`typeid(*via_left)` 显示动态类型 `Diamond`。

- 实验 5：`typeid(*p)` 显示 `Dog`；`dynamic_cast<Cat*>` 返回 `nullptr`，`dynamic_cast<Cat&>` 抛 `bad_cast`。

- 平台与方差：数值来自 Apple M2 Pro / macOS Darwin 25.5（arm64，Apple clang 17，16 KB 页），vtable 内省仅适用于 **Itanium C++ ABI**（Linux/macOS 的 GCC/Clang）；MSVC 的布局与槽位不同。示例无计时实验，重复运行数值稳定（地址每次 ASLR 变化属正常）。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| 一个对象有几个 vptr？ | 每个多态基类子对象一个：单继承多态对象 1 个，多重继承 N 个多态基类 N 个，非多态类 0 个 |
| vtable 每个对象一份吗？ | 不是：每个多态类（精确说每个多态基类子对象）一份，编译期生成在只读数据段；对象里只有指向它的 vptr |
| vtable 的 `-1` / `-2` 槽是什么？ | Itanium ABI：`-1` 是 `type_info*`（RTTI），`-2` 是 offset-to-top（子对象回完整对象的偏移） |
| 构造 / 析构中调虚函数为什么是当前层版本？ | vptr 在每层构造/析构时被改写，此刻动态类型就是当前层（真题 001） |
| 基类析构为什么必须 virtual？ | `delete base_ptr` 需经 vtable 找到派生析构；非虚析构是 UB，派生成员不析构 |
| 多重继承下 `Base2*` 与 `Derived*` 地址为什么不同？ | Base2 子对象有非零偏移；`static_cast` 调整 `this`，`reinterpret_cast` / `void*` 中转不调整 |
| 虚调用开销到底是什么？ | 两次依赖加载（vptr、槽）+ 间接跳转；可被 `final`/LTO 去虚化，megamorphic 调用点预测代价高 |
| `dynamic_cast` 为什么慢？ | 运行期沿继承图检查并调整指针，远高于虚调用；热路径用标签/variant 替代 |
| `typeid(*p)` 与 `typeid(T)` 区别？ | 前者对多态类型运行期解析动态类型，后者编译期静态类型；`typeid` 去 cv/引用 |
| 什么是对象切片？ | 按值传 `Derived` 给 `Base` 只拷贝基类子对象，vptr 变成基类，派生部分丢失 |
| 空类为什么 `sizeof` 是 1？EBO 条件？ | 保证不同对象地址不同；空基类可优化为 0 字节，空成员仍占 1 字节（`[[no_unique_address]]` 可消除） |
| 多态对象能 `memcpy` / 序列化吗？ | 不能按字节跨进程解释：带 vptr、非 trivially copyable；序列化用 standard-layout 数据或显式协议 |
| 虚继承的代价？ | 共享子对象需额外偏移间接层，对象更大，构造顺序特殊（虚基类最先），不能 standard-layout |

## 7. 深度追问记录（落盘区）

> 学习过程中的追问按行追加；结论影响主文时同步改正文并注明。已记录 1 条。

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|
| 2026-11-03 | 实验 4 的四个类各自内存怎么排？ | `VBase` 16 / `Left` 32 / `Right` 32 / `Diamond` 48；`Diamond` 偏移 `vptr@0 · l@8 · vptr@16 · r@24 · d@28 · vptr@32 · v@40`，`d` 复用 `Right` 尾部 padding；虚基偏移由 vtable 负槽给出（Diamond 的 Left 子对象 `[-3]=32`、Right 子对象 `[-3]=16`、独立 Left `[-3]=16`），`[-2]` 依次为 0 / -16 / -32（GCC 12.2 x86-64 实测，与 §5 的 macOS 数值一致） | §4.4（虚基偏移机制，删除 vbptr 表述） |

## 8. 参考

- cppreference: [virtual function](https://en.cppreference.com/w/cpp/language/virtual) / [derived classes](https://en.cppreference.com/w/cpp/language/derived_class) / [dynamic_cast](https://en.cppreference.com/w/cpp/language/dynamic_cast) / [type_info](https://en.cppreference.com/w/cpp/types/type_info)（核实日期 2026-11-03）

- Itanium C++ ABI: [Virtual Table Layout](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#vtable)（vtable 地址点、`-1`/`-2` 槽、thunk 与 VTT 的依据；核实日期 2026-11-03）

- 《深度探索 C++ 对象模型》（Lippman）——对象布局、vptr/vtable、多重继承章节

- 《Effective C++》Item 7（虚析构）、Item 20（按 `const` 引用传参，避免对象切片）

- 关联真题：[interview/cpp/001](../../../interview/cpp/001-constructor-virtual-function.md)（构造/析构中调虚函数）

- 关联知识点：[smart-pointers](../smart-pointers/README.md)（EBO 与 `unique_ptr`）、[move-semantics](../move-semantics/README.md)（对象生命周期）、[false-sharing](../../low-latency/false-sharing/README.md)（对象布局与缓存行）
