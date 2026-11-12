// example.cpp — const 正确性与关键字细节
// 编译: clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example

#include <cstddef>
#include <cstdio>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// 1. const / 非 const 成员函数重载
class Buffer {
public:
    int& operator[](std::size_t i) { return data_[i]; }
    const int& operator[](std::size_t i) const { return data_[i]; }

    // 2. mutable：const 成员函数里缓存
    int sum() const {
        if (!cache_valid_) {
            sum_ = 0;
            for (int v : data_) sum_ += v;
            cache_valid_ = true;
        }
        return sum_;
    }

    void set(int i, int v) {
        data_[i] = v;
        cache_valid_ = false;
    }

private:
    int data_[4]{1, 2, 3, 4};
    mutable int sum_ = 0;
    mutable bool cache_valid_ = false;
};

// 3. 顶层 const 在模板推导中被丢弃
template <typename T>
constexpr bool deduced_is_int(T) {
    return std::is_same_v<T, int>;
}

// 4. explicit 阻止隐式转换
struct Meters {
    explicit Meters(double v) : value(v) {}
    double value;
};

struct Widget {
    static inline int count = 0;   // 5. inline static 成员，头文件可定义
    int id = 0;
};

// 6. const 成员函数可以修改 mutable 成员，但不能修改普通成员
struct ConstProbe {
    int normal = 0;
    mutable int counter = 0;

    void touch() const {
        ++counter;        // OK
        // ++normal;      // 编译错误
    }
};

// 7. const 返回值与移动：返回 const 值会抑制移动
struct MoveProbe {
    MoveProbe() = default;
    MoveProbe(const MoveProbe&) { std::printf("copy\n"); }
    MoveProbe(MoveProbe&&) noexcept { std::printf("move\n"); }
};

MoveProbe make_probe() { return MoveProbe{}; }             // RVO：不调用拷贝/移动
const MoveProbe make_const_probe() { return MoveProbe{}; } // 返回 const 值

}  // namespace

int main() {
    std::printf("=== 1. const 重载 ===\n");
    Buffer buf;
    buf[0] = 10;                    // 非 const 版本，返回 int&
    const Buffer& cref = buf;
    int v = cref[0];                // const 版本，返回 const int&
    std::printf("buf[0]=%d cref[0]=%d\n", buf[0], v);

    std::printf("\n=== 2. mutable 缓存 ===\n");
    std::printf("sum=%d sum=%d\n", cref.sum(), cref.sum());

    std::printf("\n=== 3. 顶层 const 推导 ===\n");
    const int ci = 1;
    constexpr int i = 2;
    static_assert(deduced_is_int(ci));
    static_assert(deduced_is_int(i));
    std::printf("T(ci)=int, T(i)=int（顶层 const 被丢弃）\n");

    std::printf("\n=== 4. explicit ===\n");
    static_assert(!std::is_convertible_v<double, Meters>);
    static_assert(std::is_constructible_v<Meters, double>);
    Meters m{1.5};
    // Meters bad = 1.5;   // 编译错误：explicit 阻止隐式转换
    std::printf("Meters{1.5}.value=%.1f，隐式转换被禁止\n", m.value);

    std::printf("\n=== 5. inline static 成员 ===\n");
    Widget w1, w2;
    Widget::count = 2;
    std::printf("Widget::count=%d sizeof(Widget)=%zu\n", Widget::count, sizeof(Widget));
    (void)w1;
    (void)w2;

    std::printf("\n=== 6. const 成员函数 + mutable ===\n");
    ConstProbe cp;
    cp.touch();
    cp.touch();
    std::printf("counter=%d normal=%d\n", cp.counter, cp.normal);

    std::printf("\n=== 7. const 对象抑制移动 ===\n");
    auto a = make_probe();          // C++17 保证复制消除：不调用拷贝/移动
    auto b = make_const_probe();    // 同样直接构造
    MoveProbe p;
    const MoveProbe cprobe;
    std::printf("move 非 const：");
    MoveProbe from_p = std::move(p);       // 调用移动构造
    std::printf("move const   ：");
    MoveProbe from_cp = std::move(cprobe); // const T&& 绑不上 T&&，退化拷贝
    (void)a;
    (void)b;
    (void)from_p;
    (void)from_cp;
}
