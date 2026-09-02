// 追问一：怎样的 unique_ptr 删除器才算"零开销"？EBO 是什么机制？
// 编译: g++ -std=c++20 -O2 -Wall example-ebo-deleter.cpp -o ebo_deleter && ./ebo_deleter
//
// 结论速览：
//   1. 零开销 = sizeof(unique_ptr<T, D>) == sizeof(T*) 且调用可内联。
//   2. 条件：D 是"空类"（无非静态数据成员、无虚函数、无虚基类）。
//   3. 机制：unique_ptr 的实现把删除器作为【基类】而不是【成员】持有，
//      空基类在派生对象里占 0 字节 —— 这就是 EBO（Empty Base Optimization，空基类优化）。
//   4. 函数指针删除器：+8 字节（多存一个指针）+ 每次析构一次间接调用（不可内联）。
//   5. C++20 起可用 [[no_unique_address]] 在"成员"位置达到同等效果。

#include <cstdio>
#include <iostream>
#include <memory>

// ========== 第一部分：EBO 本体 ==========
// 空类作为独立对象必须占 ≥1 字节（保证不同对象地址唯一），
// 但作为基类时可以占 0 字节 —— 编译器允许基类子对象与首成员共用地址。
struct Empty {};

struct Contains {        // 成员方式持有空类
    Empty e;
    int x;
};

struct Inherits : private Empty {  // 继承方式持有空类（unique_ptr 的做法）
    int x;
};

struct ContainsNoAddr {  // C++20 [[no_unique_address]]：成员版 EBO
    [[no_unique_address]] Empty e;
    int x;
};

static_assert(sizeof(Empty) == 1);          // 独立空类: 1 字节
static_assert(sizeof(Contains) == 8);       // 成员: 空类占 1 + 对齐填充 → 8
static_assert(sizeof(Inherits) == 4);       // 基类: 空基类占 0 → 只有 int
static_assert(sizeof(ContainsNoAddr) == 4); // C++20 属性 → 同样 4

// ========== 第二部分：删除器的 sizeof 全表 ==========
struct Widget { int x = 1; };

struct StatelessDeleter {              // 无状态函数对象: 空类
    void operator()(Widget* p) const { delete p; }
};

struct StatefulDeleter {               // 有状态: 多存一个 FILE*
    FILE* log;
    void operator()(Widget* p) const {
        std::fprintf(log, "delete widget\n");
        delete p;
    }
};

void experiment_two() {
    int captured = 0;
    auto cap_lambda = [captured](Widget* p) { delete p; (void)captured; };  // 捕获 int → 有状态
    auto nocap_lambda = [](Widget* p) { delete p; };                        // 无捕获 → 空类

    std::cout << "[实验二] 删除器对 sizeof(unique_ptr<Widget, D>) 的影响\n";
    std::cout << "  默认删除器(default_delete)      = " << sizeof(std::unique_ptr<Widget>) << "\n";
    std::cout << "  无状态函数对象 StatefulDeleter  = " << sizeof(std::unique_ptr<Widget, StatelessDeleter>) << "\n";
    std::cout << "  无捕获 lambda                   = " << sizeof(std::unique_ptr<Widget, decltype(nocap_lambda)>) << "\n";
    std::cout << "  函数指针 void(*)(Widget*)       = " << sizeof(std::unique_ptr<Widget, void (*)(Widget*)>) << "  ← 两个指针\n";
    std::cout << "  有状态函数对象(含 FILE*)        = " << sizeof(std::unique_ptr<Widget, StatefulDeleter>) << "  ← 指针+状态\n";
    std::cout << "  捕获 int 的 lambda              = " << sizeof(std::unique_ptr<Widget, decltype(cap_lambda)>) << "  ← 指针+捕获\n";

    static_assert(sizeof(std::unique_ptr<Widget>) == sizeof(Widget*));
    static_assert(sizeof(std::unique_ptr<Widget, StatelessDeleter>) == sizeof(Widget*));
    static_assert(sizeof(std::unique_ptr<Widget, decltype(nocap_lambda)>) == sizeof(Widget*));
    static_assert(sizeof(std::unique_ptr<Widget, void (*)(Widget*)>) == 2 * sizeof(Widget*));
    static_assert(sizeof(std::unique_ptr<Widget, StatefulDeleter>) == 2 * sizeof(Widget*));
    static_assert(sizeof(std::unique_ptr<Widget, decltype(cap_lambda)>) == 2 * sizeof(Widget*));

    // 运行期两种删除器都能正常工作；区别在调用路径：
    // 无状态 functor 的 operator() 是普通成员调用, 编译器可内联；
    // 函数指针是一次间接跳转, 且阻止内联。
    { std::unique_ptr<Widget, void (*)(Widget*)> p(new Widget, nocap_lambda); }
    { std::unique_ptr<Widget, decltype(nocap_lambda)> q(new Widget, nocap_lambda); }
    std::cout << "  → 两种删除器行为一致, 差别只在存储与调用开销\n";
}

int main() {
    std::cout << "== 追问一: 删除器零开销与 EBO ==\n";

    std::cout << "[实验一] EBO 本体: 同样的空类, 成员 vs 基类\n";
    std::cout << "  sizeof(Empty)          = " << sizeof(Empty) << "  (独立空类最少 1 字节)\n";
    std::cout << "  sizeof(Contains)       = " << sizeof(Contains) << "  (作成员: 1+填充)\n";
    std::cout << "  sizeof(Inherits)       = " << sizeof(Inherits) << "  (作基类: EBO → 0)\n";
    std::cout << "  sizeof(ContainsNoAddr) = " << sizeof(ContainsNoAddr) << "  (C++20 [[no_unique_address]] → 成员也能 0)\n";

    experiment_two();

    std::cout << "\n要点复述:\n"
              << "  1. 零开销删除器 = 空类 functor 或无捕获 lambda (sizeof==T* 且可内联)\n"
              << "  2. 函数指针: +8 字节 + 间接调用; 仅在需要运行时切换删除器/跨 ABI 时才值得\n"
              << "  3. 捕获 lambda: 捕获内容计入对象大小\n"
              << "  4. EBO 机制: unique_ptr 实现以私有基类持有删除器, 空基类占 0 字节\n";
    return 0;
}
