// example23.cpp — C++23 高价值库特性
// 编译: clang++ -std=c++23 -O2 -Wall example23.cpp -o example23 && ./example23
// 说明: 只使用本机 libc++ 200100 已实现的特性的子集（expected/flat_map/mdspan/print/ranges::to）。

#include <cstddef>
#include <expected>
#include <flat_map>
#include <mdspan>
#include <optional>
#include <print>
#include <ranges>
#include <string>
#include <vector>

struct Error {
    int code;
};

std::expected<int, Error> parse(int x) {
    if (x < 0) return std::unexpected(Error{x});
    return x * 2;
}

int main() {
    // expected + 单子操作
    auto parsed = parse(21).and_then([](int x) -> std::expected<int, Error> {
        return x + 1;
    });
    if (!parsed) {
        std::println("parse failed: {}", parsed.error().code);
        return 1;
    }

    // flat_map：连续内存的有序映射
    std::flat_map<int, std::string> levels;
    levels.insert({100, "bid"});
    levels.insert({101, "ask"});
    levels.insert({102, "last"});

    // mdspan：多维零拷贝视图
    int data[6]{1, 2, 3, 4, 5, 6};
    std::mdspan<int, std::extents<std::size_t, 2, 3>> grid{data};

    // ranges::to：惰性视图物化
    std::vector<int> v{1, 2, 3};
    auto doubled = v
        | std::views::transform([](int x) { return x * 2; })
        | std::ranges::to<std::vector>();

    // optional 单子操作
    std::optional<int> opt{3};
    auto squared = opt.transform([](int x) { return x * x; });

    // string::resize_and_overwrite：原地重设
    std::string text = "abcdef";
    text.resize_and_overwrite(3, [](char* p, std::size_t) {
        p[0] = 'x';
        return std::size_t{3};
    });

    // views::zip：多序列同步遍历
    std::vector<int> a{1, 2, 3};
    std::vector<int> b{10, 20, 30};
    int zip_sum = 0;
    for (auto [x, y] : std::views::zip(a, b)) zip_sum += x + y;

    std::println("expected={} flat_map={} mdspan={} to={} optional={} text={} zip={}",
                 *parsed, levels.size(), grid[1, 2], doubled.size(),
                 squared.value_or(-1), text, zip_sum);
}
