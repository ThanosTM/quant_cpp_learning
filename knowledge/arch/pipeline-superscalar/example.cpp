// 流水线、乱序执行与 IPC — 可运行示例
// 编译: g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
//
// 本机: Apple M2 Pro / macOS / arm64（Apple clang 17）。
// 两个实验：
//   一、浮点归约：1/2/4/8 个累加器，观察依赖链 vs ILP
//   二、纯 ALU 依赖链：1 条链 vs 8 条独立链，排除访存影响

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
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

#if defined(__aarch64__)
#define KEEP_FP(x) asm volatile("" : "+w"(x))
#else
#define KEEP_FP(x) asm volatile("" : "+x"(x))
#endif

// 1 个累加器：每次加法都依赖上一次结果，形成一条长依赖链。
__attribute__((noinline)) double sum1(const double* data, std::size_t n) {
    double s = 0.0;
    NO_VECTORIZE
    for (std::size_t i = 0; i < n; ++i) {
        s += data[i];
        KEEP_FP(s);  // 阻止 SLP 向量化，只留下标量依赖链
    }
    return s;
}

__attribute__((noinline)) double sum2(const double* data, std::size_t n) {
    double s0 = 0.0, s1 = 0.0;
    std::size_t i = 0;
    NO_VECTORIZE
    for (; i + 2 <= n; i += 2) {
        s0 += data[i];
        s1 += data[i + 1];
        KEEP_FP(s0);
        KEEP_FP(s1);
    }
    for (; i < n; ++i) {
        s0 += data[i];
        KEEP_FP(s0);
    }
    return s0 + s1;
}

__attribute__((noinline)) double sum4(const double* data, std::size_t n) {
    double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
    std::size_t i = 0;
    NO_VECTORIZE
    for (; i + 4 <= n; i += 4) {
        s0 += data[i];
        s1 += data[i + 1];
        s2 += data[i + 2];
        s3 += data[i + 3];
        KEEP_FP(s0);
        KEEP_FP(s1);
        KEEP_FP(s2);
        KEEP_FP(s3);
    }
    for (; i < n; ++i) {
        s0 += data[i];
        KEEP_FP(s0);
    }
    return (s0 + s1) + (s2 + s3);
}

__attribute__((noinline)) double sum8(const double* data, std::size_t n) {
    double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
    double s4 = 0.0, s5 = 0.0, s6 = 0.0, s7 = 0.0;
    std::size_t i = 0;
    NO_VECTORIZE
    for (; i + 8 <= n; i += 8) {
        s0 += data[i];
        s1 += data[i + 1];
        s2 += data[i + 2];
        s3 += data[i + 3];
        s4 += data[i + 4];
        s5 += data[i + 5];
        s6 += data[i + 6];
        s7 += data[i + 7];
        KEEP_FP(s0);
        KEEP_FP(s1);
        KEEP_FP(s2);
        KEEP_FP(s3);
        KEEP_FP(s4);
        KEEP_FP(s5);
        KEEP_FP(s6);
        KEEP_FP(s7);
    }
    for (; i < n; ++i) {
        s0 += data[i];
        KEEP_FP(s0);
    }
    return ((s0 + s1) + (s2 + s3)) + ((s4 + s5) + (s6 + s7));
}

// 纯 ALU 链：连续执行 n 次浮点加法。
__attribute__((noinline)) double chain1(std::size_t n) {
    double x = 1.0;
    NO_VECTORIZE
    for (std::size_t i = 0; i < n; ++i) {
        x += 1.0;
        KEEP_FP(x);
    }
    return x;
}

// 8 条独立链：每个迭代做 8 次互不依赖的加法。
__attribute__((noinline)) double chain8(std::size_t n) {
    double a0 = 1.0, a1 = 1.0, a2 = 1.0, a3 = 1.0;
    double a4 = 1.0, a5 = 1.0, a6 = 1.0, a7 = 1.0;
    std::size_t i = 0;
    NO_VECTORIZE
    for (; i + 8 <= n; i += 8) {
        a0 += 1.0;
        a1 += 1.0;
        a2 += 1.0;
        a3 += 1.0;
        a4 += 1.0;
        a5 += 1.0;
        a6 += 1.0;
        a7 += 1.0;
        KEEP_FP(a0);
        KEEP_FP(a1);
        KEEP_FP(a2);
        KEEP_FP(a3);
        KEEP_FP(a4);
        KEEP_FP(a5);
        KEEP_FP(a6);
        KEEP_FP(a7);
    }
    for (; i < n; ++i) {
        a0 += 1.0;
        KEEP_FP(a0);
    }
    return ((a0 + a1) + (a2 + a3)) + ((a4 + a5) + (a6 + a7));
}

volatile double g_sink = 0.0;

template <typename Fn>
double median_ms(Fn fn, int reps) {
    double sink = fn();  // warmup

    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(reps));
    for (int r = 0; r < reps; ++r) {
        const std::uint64_t t0 = now_ns();
        sink += fn();
        const std::uint64_t t1 = now_ns();
        times.push_back(static_cast<double>(t1 - t0) / 1e6);
    }
    g_sink = sink;  // 防止整段计算被优化掉

    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

void print_reduction(const char* name, double ms, std::size_t n, double base_ms) {
    const double ns_per_elt = ms * 1e6 / static_cast<double>(n);
    std::printf("  %-8s %8.2f ms  %6.3f ns/elt  %5.2fx\n", name, ms,
                ns_per_elt, base_ms / ms);
}

}  // namespace

int main() {
    constexpr std::size_t kN = 8000000;  // 8M double = 64 MB
    constexpr int kReps = 5;

    std::printf("== 流水线、乱序执行与 IPC ==\n");
    std::printf("\n[实验一] 浮点归约：累加器数量与 ILP（N=%zu double，%.0f MB）\n",
                kN, static_cast<double>(kN * sizeof(double)) / (1024.0 * 1024.0));
    std::printf("  版本       时间        ns/元素   相对 1 累加器\n");

    std::vector<double> data(kN, 1.0);

    const double ms1 = median_ms([&] { return sum1(data.data(), kN); }, kReps);
    const double ms2 = median_ms([&] { return sum2(data.data(), kN); }, kReps);
    const double ms4 = median_ms([&] { return sum4(data.data(), kN); }, kReps);
    const double ms8 = median_ms([&] { return sum8(data.data(), kN); }, kReps);

    print_reduction("sum1", ms1, kN, ms1);
    print_reduction("sum2", ms2, kN, ms1);
    print_reduction("sum4", ms4, kN, ms1);
    print_reduction("sum8", ms8, kN, ms1);

    constexpr std::size_t kChainN = 100000000;  // 1 亿次加法
    std::printf("\n[实验二] 纯 ALU 链：1 条链 vs 8 条独立链（N=%zu 次加法）\n",
                kChainN);

    const double chain1_ms = median_ms([&] { return chain1(kChainN); }, kReps);
    const double chain8_ms = median_ms([&] { return chain8(kChainN); }, kReps);

    std::printf("  1 条链:  %8.2f ms  %6.3f ns/add\n", chain1_ms,
                chain1_ms * 1e6 / static_cast<double>(kChainN));
    std::printf("  8 条链:  %8.2f ms  %6.3f ns/add  %5.2fx\n", chain8_ms,
                chain8_ms * 1e6 / static_cast<double>(kChainN),
                chain1_ms / chain8_ms);

    return 0;
}
