// example.cpp — C++14/17/20 特性巡礼（C++20 编译）
// 编译: clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
// 说明: 每个小节只演示「面试最常问、工程最常用」的代表特性，不追求 API 全覆盖。

#include <atomic>
#include <bit>
#include <compare>
#include <concepts>
#include <cstdint>
#include <format>
#include <iostream>
#include <latch>
#include <memory>
#include <optional>
#include <ranges>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// ---------- C++14 ----------

// 变量模板（C++14）
template <typename T>
constexpr T pi_v = T(3.1415926535897932385);

// 放宽 constexpr：循环 + 局部变量（C++14）
constexpr int sum_to(int n) {
    int s = 0;
    for (int i = 1; i <= n; ++i) s += i;
    return s;
}

// index_sequence 展开 tuple（C++14）
template <typename Tuple, std::size_t... I>
int tuple_sum_impl(const Tuple& t, std::index_sequence<I...>) {
    int values[] = {std::get<I>(t)...};
    int s = 0;
    for (int v : values) s += v;
    return s;
}

template <typename Tuple>
int tuple_sum(const Tuple& t) {
    return tuple_sum_impl(t, std::make_index_sequence<std::tuple_size<Tuple>::value>{});
}

// ---------- C++17 ----------

// inline 变量（C++17）：头文件中全程序唯一
inline int global_counter = 0;

// 折叠表达式（C++17）
template <typename... Ts>
auto fold_sum(Ts... xs) {
    return (xs + ... + 0);
}

struct Point {
    int x;
    int y;
    auto operator<=>(const Point&) const = default;   // C++20
};

// ---------- C++20 ----------

template <typename T>
concept Number = std::integral<T> || std::floating_point<T>;

template <Number T>
constexpr T twice(T x) {
    return x + x;
}

consteval int square(int n) {          // C++20 立即函数
    return n * n;
}

void print_section(std::string_view title) {
    std::cout << std::format("\n[{}]\n", title);
}

int main() {
    print_section("C++14");
    auto generic = [](auto x) { return x + x; };          // 泛型 lambda
    auto captured = [value = std::move(generic(21))] {    // 初始化捕获
        return value;
    };
    std::unique_ptr<int> owned = std::make_unique<int>(7);
    auto t14 = std::make_tuple(1, 2, 3);
    static_assert(sum_to(100) == 5050);
    std::cout << std::format(
        "generic={} pi={:.4f} constexpr={} unique={} tuple={} capture={}\n",
        generic(21), pi_v<double>, sum_to(100), *owned, tuple_sum(t14), captured());

    print_section("C++17");
    std::tuple<int, double, std::string> t17{1, 2.5, "three"};
    auto [a, b, c] = t17;                                  // 结构化绑定
    if constexpr (std::is_integral_v<decltype(a)>) {        // 编译期分支
        std::cout << std::format("binding: {} {} {}\n", a, b, c);
    }
    std::optional<int> opt{7};
    std::string_view sv{"view"};
    std::variant<int, std::string> var{std::string{"variant"}};
    std::pair pair{1, 2.5};                                // CTAD
    global_counter = 1;
    std::cout << std::format(
        "optional={} view={} variant={} fold={} ctad={} inline={}\n",
        opt.value_or(-1), sv, std::get<std::string>(var), fold_sum(1, 2, 3, 4),
        pair.second, global_counter);

    print_section("C++20");
    static_assert(Number<int> && Number<double>);
    static_assert(square(5) == 25);

    std::vector<int> v{1, 2, 3, 4, 5};
    auto evens = v
        | std::views::filter([](int x) { return x % 2 == 0; })
        | std::views::transform([](int x) { return x * x; });
    int range_sum = 0;
    for (int x : evens) range_sum += x;

    std::span<int> sp{v};
    Point p1{1, 2}, p2{1, 3};
    bool less = p1 < p2;                                    // <=>

    int plain = 0;
    std::atomic_ref<int> ref{plain};                        // 非原子对象的原子访问
    ref.fetch_add(1, std::memory_order_relaxed);

    std::latch ready{2};
    std::jthread t1([&] { ready.count_down(); });
    std::jthread t2([&] { ready.count_down(); });
    ready.wait();

    auto loc = std::source_location::current();
    float f = 1.0f;
    auto bits = std::bit_cast<std::uint32_t>(f);

    std::cout << std::format(
        "concept={} ranges={} span={} <=>={} atomic={} bitcast={:#x} loc={}\n",
        twice(21), range_sum, sp.size(), less, plain, bits, loc.function_name());
}
