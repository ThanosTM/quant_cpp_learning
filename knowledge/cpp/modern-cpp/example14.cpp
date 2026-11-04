// example14.cpp — C++14 重点特性
// 编译: clang++ -std=c++14 -O2 -Wall example14.cpp -o example14 && ./example14

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <shared_mutex>
#include <tuple>
#include <utility>

template <typename T>
constexpr T pi_v = T(3.1415926535897932385);

constexpr int sum_to(int n) {
    int s = 0;
    for (int i = 1; i <= n; ++i) s += i;
    return s;
}

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

int main() {
    static_assert(sum_to(100) == 5050, "relaxed constexpr");

    auto generic = [](auto&& x) { return x + x; };       // 泛型 lambda
    auto captured = [value = generic(21)] { return value; }; // 初始化捕获

    std::unique_ptr<int> owned = std::make_unique<int>(7);
    auto t = std::make_tuple(1, 2, 3);
    int mask = 0b1010'1010;                              // 二进制字面量 + 分隔符

    using namespace std::chrono_literals;
    auto timeout = 100ms;                                // chrono 字面量

    int x = 1, y = 2;
    int old = std::exchange(x, y);                       // 交换并返回旧值

    std::shared_timed_mutex m;
    {
        std::shared_lock<std::shared_timed_mutex> lk(m); // 读写锁的读锁
    }

    std::cout << "generic=" << generic(21)
              << " pi=" << pi_v<double>
              << " constexpr=" << sum_to(100)
              << " unique=" << *owned
              << " tuple=" << tuple_sum(t)
              << " capture=" << captured()
              << " bin=" << mask
              << " timeout=" << timeout.count()
              << " exchange=" << old << '\n';
}
