// example.cpp — constexpr / consteval / constinit / if consteval
// 编译: clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace {

// 1. constexpr 函数：编译期与运行期都能用
constexpr int factorial(int n) {
    int r = 1;
    for (int i = 2; i <= n; ++i) r *= i;
    return r;
}

// 2. 编译期生成数组
constexpr std::array<int, 10> make_squares() {
    std::array<int, 10> a{};
    for (std::size_t i = 0; i < a.size(); ++i) a[i] = static_cast<int>(i * i);
    return a;
}

constexpr auto kSquares = make_squares();   // 编译期初始化
static_assert(kSquares[9] == 81);

// 3. consteval：只能编译期求值
consteval int square(int n) { return n * n; }

// 4. constinit：静态初始化阶段完成，变量仍可修改
constinit int g_counter = 100;

// 5. is_constant_evaluated：编译期走快路径（C++23 的 if consteval 更直观）
constexpr int compile_or_runtime(int n) {
    if (std::is_constant_evaluated()) {
        return n * 2;      // 编译期：直接算
    }
    return n + 1;          // 运行期：走另一条路径
}

// 6. 编译期字符串哈希（FNV-1a）
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

// 7. constexpr 变量模板
template <typename T>
constexpr T kPi = T(3.14159265358979323846);

// 8. constexpr lambda（C++17）
constexpr auto kDouble = [](int x) { return x * 2; };

// 编译期断言
static_assert(factorial(5) == 120);
static_assert(square(7) == 49);
static_assert(compile_or_runtime(10) == 20);   // 常量表达式上下文
static_assert(hash_literal("order") == fnv1a("order", 5));

}  // namespace

int main() {
    std::printf("=== 1. constexpr 函数 ===\n");
    constexpr int f5 = factorial(5);        // 编译期
    int n = 6;
    int f6 = factorial(n);                  // 运行期
    std::printf("factorial(5)=%d factorial(6)=%d\n", f5, f6);

    std::printf("\n=== 2. 编译期数组 ===\n");
    std::printf("kSquares[0..9]=");
    for (int v : kSquares) std::printf("%d ", v);
    std::printf("\n");

    std::printf("\n=== 3. consteval ===\n");
    constexpr int s9 = square(9);
    std::printf("square(9)=%d\n", s9);

    std::printf("\n=== 4. constinit ===\n");
    std::printf("g_counter=%d\n", g_counter);
    g_counter += 1;                          // 可以修改
    std::printf("after +=1: %d\n", g_counter);

    std::printf("\n=== 5. is_constant_evaluated ===\n");
    constexpr int compile_time = compile_or_runtime(10);
    int runtime_arg = 10;
    int runtime_time = compile_or_runtime(runtime_arg);
    std::printf("compile=%d runtime=%d\n", compile_time, runtime_time);

    std::printf("\n=== 6. 编译期字符串哈希 ===\n");
    constexpr auto h_order = hash_literal("order");
    constexpr auto h_trade = hash_literal("trade");
    std::printf("hash(order)=%u hash(trade)=%u\n", h_order, h_trade);

    std::printf("\n=== 7. constexpr 变量模板 / lambda ===\n");
    std::printf("kPi<double>=%.5f kDouble(21)=%d\n", kPi<double>, kDouble(21));

    std::printf("\n=== 8. 类型属性 ===\n");
    std::printf("factorial 可常量求值=%d kSquares 是 const=%d\n",
                static_cast<int>(std::is_invocable_v<decltype(factorial), int>),
                static_cast<int>(std::is_const_v<std::remove_reference_t<decltype(kSquares)>>));
}
