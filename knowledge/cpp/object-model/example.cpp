// example.cpp —— 对象模型五组演示：内存布局 / vptr 与 vtable / 多重继承 this 调整 / 虚继承 / RTTI
// 编译: g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
//       (macOS 可用 clang++ 替换 g++)
//
// 说明: vtable 内省与布局数值基于 Itanium C++ ABI（Linux / macOS 的 GCC / Clang）；
//       MSVC 的 vfptr / vbtable 布局不同，内省部分用 __GNUG__ 条件编译隔离。

#include <cstddef>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <typeinfo>

namespace {

void print_sep(const char* title) {
    std::cout << "\n== " << title << " ==\n";
}

#if defined(__GNUG__) && !defined(_WIN32)
// Itanium ABI: 多态对象起始处是 vptr；vtable[-2] = offset-to-top，vtable[-1] = type_info*
const void* vptr_of(const void* obj) {
    const void* vp = nullptr;
    std::memcpy(&vp, obj, sizeof(vp));
    return vp;
}

std::ptrdiff_t offset_to_top(const void* vptr) {
    std::ptrdiff_t delta = 0;
    std::memcpy(&delta, static_cast<const char*>(vptr) - 2 * sizeof(void*), sizeof(delta));
    return delta;
}

const std::type_info& rtti_of(const void* vptr) {
    const std::type_info* ti = nullptr;
    std::memcpy(&ti, static_cast<const char*>(vptr) - sizeof(void*), sizeof(ti));
    return *ti;
}

const void* vtable_slot(const void* vptr, int index) {
    const void* fn = nullptr;
    std::memcpy(&fn, static_cast<const char*>(vptr) + index * sizeof(void*), sizeof(fn));
    return fn;
}
#endif

// ---------- 实验 1: 内存布局与空基类优化 ----------
struct Empty {};
struct Plain {
    int a;
    char c;
};
struct Polymorphic {
    virtual ~Polymorphic() = default;
    int a;
};
struct WithEbo : Empty {
    int x;
};
struct WithoutEbo {
    Empty e;
    int x;
};

void experiment_layout() {
    std::cout << "sizeof(Empty)       = " << sizeof(Empty) << "  (空类至少 1 字节)\n";
    std::cout << "sizeof(Plain)       = " << sizeof(Plain) << "  alignof=" << alignof(Plain)
              << "  (int 4 + char 1 + padding 3)\n";
    std::cout << "sizeof(Polymorphic) = " << sizeof(Polymorphic)
              << "  (vptr 8 + int 4 + padding 4)\n";
    std::cout << "sizeof(WithEbo)     = " << sizeof(WithEbo) << "  sizeof(WithoutEbo) = "
              << sizeof(WithoutEbo) << "  (空基类优化: 4 vs 8)\n";
    std::cout << "is_standard_layout: Plain=" << std::is_standard_layout_v<Plain>
              << " Polymorphic=" << std::is_standard_layout_v<Polymorphic>
              << "  (有虚函数即非 standard-layout)\n";

    static_assert(sizeof(Empty) == 1);
    static_assert(sizeof(WithEbo) == sizeof(int));  // EBO：空基类不占存储
    static_assert(!std::is_standard_layout_v<Polymorphic>);
}

// ---------- 实验 2: 单继承下的 vptr 与 vtable ----------
struct A {
    virtual ~A() = default;
    virtual int f() const { return 1; }
    virtual int g() const { return 10; }
};

struct B : A {
    int f() const override { return 2; }  // 只重写 f，g 复用基类实现
};

#if defined(__GNUG__) && !defined(_WIN32)
void experiment_vtable() {
    A a;
    B b;
    const void* va = vptr_of(&a);
    const void* vb = vptr_of(&b);

    std::cout << "&a=" << &a << "  vptr(a)=" << va << "\n";
    std::cout << "&b=" << &b << "  vptr(b)=" << vb << "\n";
    std::cout << "rtti(a)=" << rtti_of(va).name() << "  rtti(b)=" << rtti_of(vb).name() << "\n";

    // 析构函数声明在前，GCC/Itanium 会为它生成 D1（complete）/ D0（deleting）两个槽，
    // 因此 f/g 落在 [2]/[3]；这是实现细节，只用于观察「槽位复用」。
    std::cout << "A slots: [0]=" << vtable_slot(va, 0) << " [1]=" << vtable_slot(va, 1)
              << " [2]f=" << vtable_slot(va, 2) << " [3]g=" << vtable_slot(va, 3) << "\n";
    std::cout << "B slots: [0]=" << vtable_slot(vb, 0) << " [1]=" << vtable_slot(vb, 1)
              << " [2]f=" << vtable_slot(vb, 2) << " [3]g=" << vtable_slot(vb, 3)
              << "  (f 槽被替换，g 槽复用)\n";

    // 手工按 vtable 槽调用，证明分派就是「取 vptr → 取槽 → 间接跳转」
    using Fn = int (*)(const A*);
    static_assert(sizeof(Fn) == sizeof(void*));
    Fn f_slot = nullptr;
    const void* raw = vtable_slot(vb, 2);
    std::memcpy(&f_slot, &raw, sizeof(f_slot));
    std::cout << "call B.vtable[2](&b) = " << f_slot(&b) << "  (期望 2)\n";
    std::cout << "normal b.f()       = " << b.f() << "  (期望 2)\n";
}
#endif

// ---------- 实验 3: 多重继承的地址调整与 thunk ----------
struct Base1 {
    virtual ~Base1() = default;
    virtual int id1() const { return 1; }
    int x = 1;
};

struct Base2 {
    virtual ~Base2() = default;
    virtual int id2() const { return 2; }
    int y = 2;
};

struct Multi : Base1, Base2 {
    int id2() const override { return 22; }
    int z = 3;
};

void experiment_multiple_inheritance() {
    Multi m;
    Base1* p1 = &m;
    Base2* p2 = &m;

    std::cout << "sizeof(Base1)=" << sizeof(Base1) << " sizeof(Base2)=" << sizeof(Base2)
              << " sizeof(Multi)=" << sizeof(Multi) << "  (每个多态基类各带一个 vptr)\n";
    std::cout << "&m=" << &m << "  p1=" << p1 << "  p2=" << p2 << "  (p2 相对 &m 偏移 +"
              << (reinterpret_cast<const char*>(p2) - reinterpret_cast<const char*>(&m)) << ")\n";
    void* raw_m = static_cast<void*>(&m);
    Base2* via_void = static_cast<Base2*>(raw_m);  // 常见 bug：经 void* 中转丢失 this 调整
    std::cout << "void* 直接转回 Base2*=" << via_void
              << "  (与 p2 不相等：static_cast 做了 this 调整，经 void* 中转则没有)\n";

#if defined(__GNUG__) && !defined(_WIN32)
    std::cout << "offset-to-top(Base1)=" << offset_to_top(vptr_of(p1))
              << "  offset-to-top(Base2)=" << offset_to_top(vptr_of(p2))
              << "  (第二基类记录负偏移)\n";
#endif

    std::cout << "p2->id2()=" << p2->id2() << "  (虚调用经 thunk 把 this 调回完整对象)\n";
    std::cout << "dynamic_cast<Base1*>(p2)=" << dynamic_cast<Base1*>(p2)
              << "  (跨基类转换会反向调整指针)\n";
    std::cout << "static_cast<void*>(p1)=" << static_cast<void*>(p1)
              << " static_cast<void*>(p2)=" << static_cast<void*>(p2)
              << "  (原始子对象地址并不相同)\n";
    std::cout << "dynamic_cast<void*>(p1)=" << dynamic_cast<void*>(p1)
              << " dynamic_cast<void*>(p2)=" << dynamic_cast<void*>(p2)
              << " same? " << (dynamic_cast<void*>(p1) == dynamic_cast<void*>(p2))
              << "  (dynamic_cast<void*> 才回到完整对象)\n";
}

// ---------- 实验 4: 虚继承的共享子对象 ----------
struct VBase {
    virtual ~VBase() = default;
    int v = 0;
};

struct Left : virtual VBase {
    int l = 1;
};

struct Right : virtual VBase {
    int r = 2;
};

struct Diamond : Left, Right {
    int d = 3;
};

void experiment_virtual_inheritance() {
    Diamond dia;
    VBase* via_left = static_cast<Left*>(&dia);
    VBase* via_right = static_cast<Right*>(&dia);

    std::cout << "sizeof(VBase)=" << sizeof(VBase) << " sizeof(Left)=" << sizeof(Left)
              << " sizeof(Right)=" << sizeof(Right) << " sizeof(Diamond)=" << sizeof(Diamond)
              << "  (虚基类子对象只存一份，Left/Right 各带虚基偏移)\n";
    std::cout << "VBase via Left=" << via_left << "  via Right=" << via_right
              << "  same? " << (via_left == via_right) << "\n";
    std::cout << "typeid(*via_left)=" << typeid(*via_left).name()
              << "  (静态类型是 VBase，动态类型是 Diamond)\n";
}

// ---------- 实验 5: RTTI 与 dynamic_cast ----------
struct RBase {
    virtual ~RBase() = default;
};
struct Dog : RBase {};
struct Cat : RBase {};

void experiment_rtti() {
    Dog dog;
    RBase* p = &dog;

    std::cout << "typeid(Dog).name()=" << typeid(Dog).name() << "  (静态类型，编译期)\n";
    std::cout << "typeid(*p).name()=" << typeid(*p).name() << "  (动态类型，运行期查 vtable[-1])\n";
    std::cout << "dynamic_cast<Dog*>(p)=" << dynamic_cast<Dog*>(p)
              << "  dynamic_cast<Cat*>(p)=" << dynamic_cast<Cat*>(p)
              << "  (指针版失败返回 nullptr)\n";

    try {
        Cat& c = dynamic_cast<Cat&>(*p);
        (void)c;
    } catch (const std::bad_cast& e) {
        std::cout << "dynamic_cast<Cat&>(*p) 抛出 bad_cast: " << e.what()
                  << "  (引用版失败抛异常)\n";
    }
}

}  // namespace

int main() {
    std::cout << "== C++ 对象模型 ==";
    print_sep("实验 1: 内存布局与 EBO");
    experiment_layout();

    print_sep("实验 2: vptr / vtable / 虚分派");
#if defined(__GNUG__) && !defined(_WIN32)
    experiment_vtable();
#else
    std::cout << "非 Itanium ABI，跳过 vtable 内省\n";
#endif

    print_sep("实验 3: 多重继承的指针调整");
    experiment_multiple_inheritance();

    print_sep("实验 4: 虚继承");
    experiment_virtual_inheritance();

    print_sep("实验 5: RTTI");
    experiment_rtti();
    return 0;
}
