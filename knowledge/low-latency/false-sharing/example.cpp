// example.cpp —— 伪共享基准：同缓存行的两个计数器 vs 各自独占缓存行
// 编译: g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
//       (macOS 可用 clang++ 替换 g++)
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

// 两个计数器紧挨着 -> 极可能落在同一条缓存行 -> 写-写伪共享
struct Unpadded {
    std::atomic<std::uint64_t> a{0};
    std::atomic<std::uint64_t> b{0};
};

// 各自独占一条缓存行。
// x86 行宽常见 64B；Apple Silicon 为 128B（偏移 0 与 64 的两个 64B 对齐成员仍同行），
// 这里用 128 保证跨平台隔离；C++17 也可查询 std::hardware_destructive_interference_size。
struct Padded {
    alignas(128) std::atomic<std::uint64_t> a{0};
    alignas(128) std::atomic<std::uint64_t> b{0};
};

template <typename Counters>
void bench(const char* name) {
    Counters c;
    constexpr std::uint64_t kIters = 20'000'000;

    auto start = std::chrono::steady_clock::now();
    std::thread t1([&] {
        for (std::uint64_t i = 0; i < kIters; ++i)
            c.a.fetch_add(1, std::memory_order_relaxed);  // 只关心写-写竞争，序无关
    });
    std::thread t2([&] {
        for (std::uint64_t i = 0; i < kIters; ++i)
            c.b.fetch_add(1, std::memory_order_relaxed);
    });
    t1.join();
    t2.join();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - start)
                  .count();

    std::cout << name << ": " << ms << " ms (a=" << c.a.load() << ", b=" << c.b.load()
              << ")\n";
}

int main() {
    std::cout << "sizeof(Unpadded)=" << sizeof(Unpadded) << ", sizeof(Padded)="
              << sizeof(Padded) << "  (Padded 每成员独占一行)\n";

    // 预热一次，减少首次运行噪声
    bench<Unpadded>("预热(Unpadded)");

    bench<Unpadded>("伪共享(Unpadded)");
    bench<Padded>  ("隔离后(Padded)  ");

    std::cout << "提示: 结果受核拓扑影响，建议跑 3 次取中位数; Linux 可 taskset 绑核减小方差。\n";
    return 0;
}
