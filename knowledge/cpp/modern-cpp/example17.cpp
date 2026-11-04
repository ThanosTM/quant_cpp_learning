// example17.cpp — C++17 重点特性
// 编译: clang++ -std=c++17 -O2 -Wall example17.cpp -o example17 && ./example17

#include <algorithm>
#include <any>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory_resource>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

inline int global_counter = 1;                 // inline 变量

template <typename... Ts>
auto fold_sum(Ts... xs) {                      // 折叠表达式
    return (xs + ... + 0);
}

template <typename T, typename = void>
struct has_size : std::false_type {};
template <typename T>
struct has_size<T, std::void_t<decltype(std::declval<T>().size())>> : std::true_type {};

int main() {
    std::tuple<int, double, std::string> t{1, 2.5, "three"};
    auto [a, b, c] = t;                        // 结构化绑定

    if constexpr (has_size<std::string>::value) {
        std::cout << "has_size=yes";
    }

    std::optional<int> opt{7};
    std::variant<int, std::string> var{std::string{"variant"}};
    std::any boxed = 3.14;
    std::string_view sv{"view"};
    std::pair pair{1, 2.5};                    // CTAD

    std::shared_mutex sm1, sm2;
    {
        std::scoped_lock lock(sm1, sm2);       // 多锁、死锁避免
    }

    auto clamped = std::clamp(15, 0, 10);
    auto g = std::gcd(12, 18);
    auto l = std::lcm(4, 6);

    std::byte byte_value{0x42};
    auto path = std::filesystem::path{"/tmp"} / "probe.txt";

    std::pmr::monotonic_buffer_resource arena{4096};
    std::pmr::vector<int> v{&arena};
    v.push_back(1);

    std::cout << " binding=" << a << ' ' << b << ' ' << c
              << " optional=" << *opt
              << " variant=" << std::get<std::string>(var)
              << " any=" << std::any_cast<double>(boxed)
              << " view=" << sv
              << " ctad=" << pair.second
              << " clamp=" << clamped
              << " gcd=" << g
              << " lcm=" << l
              << " byte=" << std::to_integer<int>(byte_value)
              << " path=" << path
              << " fold=" << fold_sum(1, 2, 3, 4)
              << " inline=" << global_counter
              << " interference=" << std::hardware_destructive_interference_size
              << '\n';
}
