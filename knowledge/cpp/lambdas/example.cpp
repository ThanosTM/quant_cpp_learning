// example.cpp — lambda 捕获、闭包布局与生命周期
// 编译: clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Counter {
    int n = 0;

    auto by_this() {
        return [this] { return ++n; };       // 捕获指针，反映外部修改
    }

    auto by_copy() {
        return [*this]() mutable { return ++n; };  // C++17：捕获对象副本
    }
};

}  // namespace

int main() {
    std::printf("=== 1. 按值 vs 按引用捕获 ===\n");
    {
        int a = 1, b = 2;
        auto by_value = [a, b] { return a + b; };
        auto by_ref = [&a, &b] { return a + b; };
        a = 10;
        std::printf("by_value=%d by_ref=%d\n", by_value(), by_ref());
    }

    std::printf("\n=== 2. mutable ===\n");
    {
        auto next = [n = 0]() mutable { return ++n; };
        std::printf("next=%d next=%d next=%d\n", next(), next(), next());
    }

    std::printf("\n=== 3. 初始化捕获：转移 unique_ptr ===\n");
    {
        auto ptr = std::make_unique<int>(7);
        auto owning = [p = std::move(ptr)] { return *p; };
        std::printf("owning=%d ptr 为空=%d\n", owning(), static_cast<int>(ptr == nullptr));
    }

    std::printf("\n=== 4. 泛型 lambda + 完美转发 ===\n");
    {
        auto forward_to = [](auto&& x) -> decltype(auto) {
            return std::forward<decltype(x)>(x);
        };
        int v = 5;
        std::printf("forward(5)=%d forward(v)=%d\n", forward_to(5), forward_to(v));
    }

    std::printf("\n=== 5. 闭包大小 ===\n");
    {
        int a = 1, b = 2;
        auto by_value = [a, b] { return a + b; };
        auto by_ref = [&a, &b] { return a + b; };
        auto with_ptr = [p = &a] { return *p; };
        std::printf("sizeof([a,b])=%zu sizeof([&a,&b])=%zu sizeof([p=&a])=%zu\n", sizeof(by_value),
                    sizeof(by_ref), sizeof(with_ptr));
    }

    std::printf("\n=== 6. constexpr lambda / 立即调用 ===\n");
    {
        constexpr auto square = [](int x) { return x * x; };
        static_assert(square(9) == 81);
        int v = [](int x) { return x + 1; }(41);
        std::printf("square(9)=%d iife=%d\n", square(9), v);
    }

    std::printf("\n=== 7. this vs *this 捕获 ===\n");
    {
        Counter c;
        auto f_this = c.by_this();
        auto f_copy = c.by_copy();
        c.n = 100;
        std::printf("by_this=%d by_copy=%d\n", f_this(), f_copy());
    }

    std::printf("\n=== 8. std::function 的类型擦除 ===\n");
    {
        std::function<int(int)> small = [](int x) { return x * 2; };
        std::string big(64, 'x');
        std::function<int(int)> large = [big, factor = 3](int x) { return static_cast<int>(big.size()) * factor + x; };
        auto plain_lambda = [](int x) { return x * 2; };
        std::printf("sizeof(std::function<int(int)>)=%zu small(21)=%d large(1)=%d\n", sizeof(small),
                    small(21), large(1));
        std::printf("普通 lambda 可拷贝=%d；std::function 要求可拷贝，move-only lambda 需 C++23 move_only_function\n",
                    static_cast<int>(std::is_copy_constructible_v<decltype(plain_lambda)>));
    }

    std::printf("\n=== 9. 算法中的 lambda ===\n");
    {
        std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};
        std::sort(v.begin(), v.end(), [](int x, int y) { return x > y; });
        auto count = std::count_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; });
        std::printf("降序: ");
        for (int x : v) std::printf("%d ", x);
        std::printf("偶数个数=%td\n", count);
    }
}
