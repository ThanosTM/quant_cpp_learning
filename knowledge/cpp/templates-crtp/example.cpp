// example.cpp — 模板进阶：CRTP、SFINAE、concepts、类型萃取
// 编译: clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example

#include <cstddef>
#include <cstdio>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// 1. CRTP：静态多态，无虚函数
template <typename Derived>
struct Shape {
    double area() const { return static_cast<const Derived*>(this)->area_impl(); }
};

struct Circle : Shape<Circle> {
    double r = 1.0;
    double area_impl() const { return 3.141592653589793 * r * r; }
};

struct Square : Shape<Square> {
    double side = 2.0;
    double area_impl() const { return side * side; }
};

// 2. CRTP 计数器：每个派生类一份独立静态计数
template <typename Derived>
struct Counted {
    Counted() { ++count; }
    Counted(const Counted&) { ++count; }
    Counted(Counted&&) noexcept { ++count; }
    ~Counted() { --count; }
    static inline int count = 0;
};

struct Order : Counted<Order> {
    int id = 0;
};

struct Trade : Counted<Trade> {
    int id = 0;
};

// 3. SFINAE + void_t：检测成员类型
template <typename T, typename = void>
struct has_value_type : std::false_type {};

template <typename T>
struct has_value_type<T, std::void_t<typename T::value_type>> : std::true_type {};

// 4. concepts：可读的约束
template <typename T>
concept HasValueType = requires { typename T::value_type; };

template <typename T>
concept Addable = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};

template <HasValueType T>
std::size_t element_hint(const T&) {
    return sizeof(typename T::value_type);
}

// 5. 标签分派：按迭代器能力选择实现
struct input_tag {};
struct random_tag {};

template <typename It>
void advance_impl(It& it, int n, input_tag) {
    while (n-- > 0) ++it;
}

template <typename It>
void advance_impl(It& it, int n, random_tag) {
    it += n;
}

template <typename It>
void advance(It& it, int n) {
    using tag = std::conditional_t<
        std::is_same_v<typename std::iterator_traits<It>::iterator_category,
                       std::random_access_iterator_tag>,
        random_tag, input_tag>;
    advance_impl(it, n, tag{});
}

// 6. 可变参数模板 + 折叠表达式
template <typename... Ts>
auto sum_all(Ts... xs) {
    return (xs + ... + 0);
}

// 7. 类模板偏特化
template <typename T>
struct TypeName {
    static constexpr const char* value = "unknown";
};

template <>
struct TypeName<int> {
    static constexpr const char* value = "int";
};

template <typename T>
struct TypeName<std::vector<T>> {
    static constexpr const char* value = "vector<T>";
};

// 8. if constexpr：按类型选择分支
template <typename T>
std::string_view category() {
    if constexpr (std::is_integral_v<T>) {
        return "integral";
    } else if constexpr (std::is_floating_point_v<T>) {
        return "floating";
    } else {
        return "other";
    }
}

}  // namespace

int main() {
    std::printf("=== 1. CRTP 静态多态 ===\n");
    Circle c;
    Square s;
    std::printf("circle area=%.3f square area=%.1f sizeof(Circle)=%zu sizeof(Square)=%zu\n",
                c.area(), s.area(), sizeof(Circle), sizeof(Square));

    std::printf("\n=== 2. CRTP 计数器 ===\n");
    {
        Order o1, o2;
        Trade t1;
        std::printf("Order::count=%d Trade::count=%d\n", Order::count, Trade::count);
    }
    std::printf("scope 结束后 Order::count=%d Trade::count=%d\n", Order::count, Trade::count);

    std::printf("\n=== 3. SFINAE 检测成员类型 ===\n");
    std::printf("has_value_type<vector<int>>=%d has_value_type<int>=%d\n",
                static_cast<int>(has_value_type<std::vector<int>>::value),
                static_cast<int>(has_value_type<int>::value));

    std::printf("\n=== 4. concepts 约束 ===\n");
    std::printf("vector<int> value_type 大小=%zu\n", element_hint(std::vector<int>{}));
    static_assert(HasValueType<std::vector<int>>);
    static_assert(!HasValueType<int>);
    static_assert(Addable<int>);
    static_assert(!Addable<void*>);
    std::printf("Addable<int>=1 Addable<void*>=0\n");

    std::printf("\n=== 5. 标签分派 ===\n");
    std::vector<int> v{1, 2, 3, 4, 5};
    auto it = v.begin();
    advance(it, 3);
    std::printf("advance 后 *it=%d\n", *it);

    std::printf("\n=== 6. 折叠表达式 ===\n");
    std::printf("sum_all(1,2,3,4)=%d\n", sum_all(1, 2, 3, 4));

    std::printf("\n=== 7. 模板特化 ===\n");
    std::printf("TypeName<int>=%s TypeName<double>=%s TypeName<vector<int>>=%s\n",
                TypeName<int>::value, TypeName<double>::value,
                TypeName<std::vector<int>>::value);

    std::printf("\n=== 8. if constexpr ===\n");
    std::printf("int=%s double=%s const char*=%s\n", category<int>().data(),
                category<double>().data(), category<const char*>().data());
}
