// 分支预测与预测失败惩罚 — 可运行示例
// 编译: g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
//
// 本机: Apple M2 Pro / macOS / arm64（Apple clang 17）。
// 三个实验：
//   一、有序 vs 无序数据：同样的条件分支，可预测性不同
//   二、无分支写法：用条件选择消除分支，观察随机数据下的差异
//   三、分支模式敏感性：有序 / 交替 / 随机三种模式

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

#if defined(__clang__)
#define NO_VECTORIZE \
    _Pragma("clang loop vectorize(disable) interleave(disable)")
#else
#define NO_VECTORIZE
#endif

// 分支版本：asm 屏障放在 if 内部，阻止编译器把它 if-convert 成 CSEL。
__attribute__((noinline)) std::uint64_t sum_branch(const std::uint8_t* data,
                                                   std::size_t n,
                                                   std::uint8_t threshold) {
    std::uint64_t sum = 0;
    NO_VECTORIZE
    for (std::size_t i = 0; i < n; ++i) {
        if (data[i] >= threshold) {
            sum += data[i];
            asm volatile("" ::: "memory");
        }
    }
    return sum;
}

// 无分支版本：条件选择由编译器生成 CSEL/CMOV，不依赖方向预测。
__attribute__((noinline)) std::uint64_t sum_branchless(const std::uint8_t* data,
                                                       std::size_t n,
                                                       std::uint8_t threshold) {
    std::uint64_t sum = 0;
    NO_VECTORIZE
    for (std::size_t i = 0; i < n; ++i) {
        sum += (data[i] >= threshold) ? static_cast<std::uint64_t>(data[i]) : 0;
    }
    return sum;
}

template <typename Fn>
double median_ms(Fn fn, int reps) {
    std::uint64_t sink = fn();  // warmup
    asm volatile("" : "+r"(sink));

    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(reps));
    for (int r = 0; r < reps; ++r) {
        const std::uint64_t t0 = now_ns();
        sink += fn();
        const std::uint64_t t1 = now_ns();
        times.push_back(static_cast<double>(t1 - t0) / 1e6);
    }
    asm volatile("" : "+r"(sink));

    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

void print_case(const char* name, double ms, std::size_t n) {
    std::printf("  %-22s %8.2f ms  %6.3f ns/elt\n", name, ms,
                ms * 1e6 / static_cast<double>(n));
}

}  // namespace

int main() {
    constexpr std::size_t kN = 32000000;  // 32 MB uint8_t
    constexpr std::uint8_t kThreshold = 128;
    constexpr int kReps = 5;

    std::printf("== 分支预测与预测失败惩罚（N=%zu，threshold=%u）==\n", kN,
                static_cast<unsigned>(kThreshold));

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);

    std::vector<std::uint8_t> random(kN);
    for (std::uint8_t& v : random) {
        v = static_cast<std::uint8_t>(dist(rng));
    }

    std::vector<std::uint8_t> sorted = random;
    std::sort(sorted.begin(), sorted.end());

    std::vector<std::uint8_t> alternating(kN);
    for (std::size_t i = 0; i < kN; ++i) {
        alternating[i] = (i & 1) ? 255 : 0;
    }

    // 实验一：可预测 vs 不可预测
    std::printf("\n[实验一] 有序 vs 无序（同样的条件分支）\n");
    const double sorted_ms =
        median_ms([&] { return sum_branch(sorted.data(), kN, kThreshold); },
                  kReps);
    const double random_ms =
        median_ms([&] { return sum_branch(random.data(), kN, kThreshold); },
                  kReps);
    print_case("sorted branch", sorted_ms, kN);
    print_case("random branch", random_ms, kN);
    std::printf("  随机/有序 = %.2fx\n", random_ms / sorted_ms);

    // 若随机数据约一半满足条件、每次误预测都付出代价，粗估单次误预测成本。
    const double fraction_taken = 0.5;
    const double extra_ns =
        (random_ms - sorted_ms) * 1e6 /
        (static_cast<double>(kN) * fraction_taken);
    std::printf("  按 50%% 误预测粗估：每次约 %.1f ns（含其它执行差异）\n",
                extra_ns);

    // 实验二：无分支写法
    std::printf("\n[实验二] 随机数据：分支 vs 无分支\n");
    const double random_branchless_ms =
        median_ms([&] { return sum_branchless(random.data(), kN, kThreshold); },
                  kReps);
    print_case("random branch", random_ms, kN);
    print_case("random branchless", random_branchless_ms, kN);
    std::printf("  分支/无分支 = %.2fx\n", random_ms / random_branchless_ms);

    // 实验三：分支模式敏感性
    std::printf("\n[实验三] 分支模式敏感性（branch 版本）\n");
    const double alternating_ms =
        median_ms([&] { return sum_branch(alternating.data(), kN, kThreshold); },
                  kReps);
    print_case("sorted", sorted_ms, kN);
    print_case("alternating", alternating_ms, kN);
    print_case("random", random_ms, kN);
    std::printf("  交替/随机 = %.2fx\n", alternating_ms / random_ms);

    return 0;
}
