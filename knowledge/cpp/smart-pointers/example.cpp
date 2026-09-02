// 智能指针与所有权 — 可运行示例
// 编译: g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
//
// 四个实验：
//   1. unique_ptr 零开销（sizeof 对比，空基类优化 vs 函数指针删除器）
//   2. 分配次数统计：shared_ptr(new T) 两次分配 vs make_shared 一次分配
//   3. 循环引用泄漏 vs weak_ptr 破环（析构输出可观察）
//   4. 多线程并发拷贝/析构 shared_ptr 副本（控制块计数原子性）

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// ---- 全局分配计数器（实验二用）：替换本翻译单元的 operator new/delete ----
static std::atomic<int> g_allocs{0};
void* operator new(std::size_t sz) { ++g_allocs; return std::malloc(sz); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t sz) { ++g_allocs; return std::malloc(sz); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

// ========== 实验一：unique_ptr 零开销 ==========
struct Widget { int x = 42; };
struct NopDeleter { void operator()(Widget* p) const { delete p; } };

void experiment_one() {
    std::cout << "[实验一] unique_ptr 大小（64 位平台，单位: 字节）\n";
    std::cout << "  sizeof(Widget*)                              = " << sizeof(Widget*) << "\n";
    std::cout << "  sizeof(unique_ptr<Widget>)                   = " << sizeof(std::unique_ptr<Widget>) << "\n";
    std::cout << "  sizeof(unique_ptr<Widget, NopDeleter>)       = " << sizeof(std::unique_ptr<Widget, NopDeleter>) << "\n";
    std::cout << "  sizeof(unique_ptr<Widget, void(*)(Widget*)>) = " << sizeof(std::unique_ptr<Widget, void (*)(Widget*)>) << "\n";
    std::cout << "  => 无状态删除器被空基类优化: 零开销; 函数指针删除器: 膨胀为两个指针\n";
}

// ========== 实验二：分配次数 ==========
struct Counted { int v = 7; };

void experiment_two() {
    std::cout << "[实验二] 构造一个 shared_ptr<Counted> 的堆分配次数\n";
    g_allocs = 0;
    { std::shared_ptr<Counted> p(new Counted); std::cout << "  shared_ptr(new Counted) : " << g_allocs.load() << " 次（对象 1 + 控制块 1）\n"; }
    g_allocs = 0;
    { auto p = std::make_shared<Counted>(); std::cout << "  make_shared<Counted>()   : " << g_allocs.load() << " 次（对象内嵌控制块）\n"; }
}

// ========== 实验三：循环引用 ==========
struct BadNode {
    std::string name;
    std::shared_ptr<BadNode> peer;  // 双向都是强引用 → 环
    explicit BadNode(std::string n) : name(std::move(n)) {}
    ~BadNode() { std::cout << "  ~BadNode " << name << "（双强引用时永不打印）\n"; }
};

struct GoodNode {
    std::string name;
    std::shared_ptr<GoodNode> next;
    std::weak_ptr<GoodNode> prev;   // 回边用 weak → 环被打破
    explicit GoodNode(std::string n) : name(std::move(n)) {}
    ~GoodNode() { std::cout << "  ~GoodNode " << name << "\n"; }
};

void experiment_three() {
    std::cout << "[实验三] 循环引用: 双强引用泄漏 vs weak_ptr 破环\n";
    {
        auto a = std::make_shared<BadNode>("A");
        auto b = std::make_shared<BadNode>("B");
        a->peer = b;
        b->peer = a;
        std::cout << "  互指后: a.use_count=" << a.use_count() << ", b.use_count=" << b.use_count() << "\n";
    }
    std::cout << "  ↑ 离开作用域，但没有 ~BadNode 输出: 计数 2→1 卡住, 泄漏\n";
    {
        auto x = std::make_shared<GoodNode>("X");
        auto y = std::make_shared<GoodNode>("Y");
        x->next = y;
        y->prev = x;
        std::cout << "  next/prev 后: x.use_count=" << x.use_count() << ", y.use_count=" << y.use_count() << "\n";
    }
    std::cout << "  ↑ weak 回边不顶计数, 环被打破\n";
}

// ========== 实验四：控制块计数原子性 ==========
void experiment_four() {
    std::cout << "[实验四] 4 线程各自并发拷贝/析构 shared_ptr 副本（各 10 万次）\n";
    auto sp = std::make_shared<int>(0);
    {
        std::vector<std::thread> pool;
        pool.reserve(4);
        for (int t = 0; t < 4; ++t) {
            pool.emplace_back([sp] {  // 按值捕获 = 拷贝, 计数 +1
                for (int i = 0; i < 100000; ++i) {
                    auto local = sp;  // 原子递增; local 析构时原子递减
                }
            });
        }
        for (auto& t : pool) t.join();
    }
    std::cout << "  全部 join 后 use_count = " << sp.use_count() << "（期望 1: 无计数丢失/重复释放）\n";
}

}  // namespace

int main() {
    std::cout << "== 智能指针与所有权 ==\n";
    experiment_one();
    experiment_two();
    experiment_three();
    experiment_four();
    return 0;
}
