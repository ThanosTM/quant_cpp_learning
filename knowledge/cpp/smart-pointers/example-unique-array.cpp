// 追问三：unique_ptr<T[]> 的用法与边界
// 编译: g++ -std=c++20 -O2 -Wall example-unique-array.cpp -o unique_array && ./unique_array
//
// 结论速览：
//   1. 数组特化 unique_ptr<T[]> 的删除器是 default_delete<T[]>（偏特化）→ 析构走 delete[]
//   2. 提供 operator[] 下标访问（无越界检查）；没有 size 元数据 → 长度自己记，遍历配 std::span
//   3. C++14 起用 std::make_unique<T[]>(n) 一次搞定，且值初始化（int 元素全为 0）
//   4. 大坑：unique_ptr<T> p(new T[n]) 【能编译】但是 UB —— delete 与 new[] 不匹配，
//      必须写 unique_ptr<T[]>；反之 new T 配 T[] 特化同样是错配
//   5. 与 std::vector<T> 的取舍：vector 自带长度/容量、可变长；unique_ptr<T[]> 更贴近
//      "固定大小裸缓冲区 + RAII"，无多余元数据（8 字节），适合与 C API 交界

#include <cstdio>
#include <iostream>
#include <memory>
#include <span>
#include <utility>

namespace {

int g_live = 0;

struct Tracked {
    int v;
    Tracked() { ++g_live; }
    ~Tracked() { --g_live; }
};

// ========== 实验一：基本用法（make_unique 值初始化 + operator[]） ==========
void experiment_one() {
    std::cout << "[实验一] make_unique<int[]>(5) 值初始化 + operator[]\n";
    auto buf = std::make_unique<int[]>(5);
    std::cout << "  刚构造: ";
    for (std::size_t i = 0; i < 5; ++i) std::cout << buf[i] << " ";  // 全 0(值初始化)
    std::cout << "\n";

    for (std::size_t i = 0; i < 5; ++i) buf[i] = static_cast<int>(i * i);
    std::cout << "  赋值后: ";
    for (std::size_t i = 0; i < 5; ++i) std::cout << buf[i] << " ";
    std::cout << "\n";

    std::cout << "  sizeof(unique_ptr<int[]>) = " << sizeof(std::unique_ptr<int[]>)
              << "（仍是一个指针, 无长度元数据）\n";

    // 所有权转移
    auto buf2 = std::move(buf);
    std::cout << "  std::move 后: buf 为空? " << std::boolalpha << !buf
              << ", buf2[3]=" << buf2[3] << "\n";

    // 遍历: 自己没有 begin/end, 借 std::span(C++20) 建立视图
    std::cout << "  span 遍历: ";
    for (int v : std::span<int>(buf2.get(), 5)) std::cout << v << " ";
    std::cout << "\n";
}

// ========== 实验二：数组特化确实走 delete[]（逐元素析构） ==========
void experiment_two() {
    std::cout << "[实验二] Tracked 数组: 特化删除器 default_delete<T[]> 调用 delete[]\n";
    {
        auto arr = std::make_unique<Tracked[]>(3);  // 每个元素默认构造
        std::cout << "  构造 Tracked[3] 后存活对象 = " << g_live << "\n";
    }
    std::cout << "  作用域结束 → 逐元素析构, 存活对象 = " << g_live << "\n";
}

// ========== 实验三：与 C API 交界（函数指针删除器, 联动追问一） ==========
void experiment_three() {
    std::cout << "[实验三] 自定义删除器守护 FILE*（unique_ptr 家族的经典用法）\n";
    if (FILE* f = std::fopen("/tmp/qj_unique_array_demo.txt", "w")) {
        // 删除器为函数指针 → sizeof 变 16 + 间接调用(见 example-ebo-deleter.cpp)
        // 换成无捕获 lambda 可回到零开销:
        //   std::unique_ptr<FILE, decltype([](FILE* p){ if(p) fclose(p); })> g(f, ...);
        std::unique_ptr<FILE, int (*)(FILE*)> guard(f, &fclose);
        std::fputs("hello unique_ptr deleter\n", guard.get());
        std::cout << "  FILE* 已交给 unique_ptr 守护, 作用域结束自动 fclose\n";
    }
}

}  // namespace

// 错误示范(注释, 不执行):
//   std::unique_ptr<int> bad(new int[10]);   // 能编译! 但析构走 delete 而非 delete[] → UB
//   std::unique_ptr<int[]> also_bad(new int); // 类型错配: 应为 unique_ptr<int>
// 若想在运行期"看见"该 UB 的典型症状: 析构计数错乱 / 非完整对象析构 / 堆损坏,
// 用 -fsanitize=address 编译运行可在多数平台直接报 allocation-deallocation mismatch。

int main() {
    std::cout << "== 追问三: unique_ptr<T[]> ==\n";
    experiment_one();
    experiment_two();
    experiment_three();
    std::cout << "\n要点复述:\n"
              << "  1. new T[n] 必须配 unique_ptr<T[]>(析构 delete[]); 配 unique_ptr<T> 是 UB\n"
              << "  2. make_unique<T[]>(n) 值初始化, 优先于 unique_ptr<T[]>(new T[n])\n"
              << "  3. operator[] 无越界检查; 无长度元数据 → 配 std::span 遍历\n"
              << "  4. 固定大小缓冲区 + C API 交界是它的主战场; 需要变长/丰富接口就用 vector\n";
    return 0;
}
