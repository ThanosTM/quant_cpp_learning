// 计时与计数器：rdtsc / clock_gettime / 性能计数器 — 可运行示例
// 编译: g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
//
// 本机: Apple M2 Pro / macOS / arm64（Apple clang 17）。
// 四个实验：
//   一、平台时钟源与换算关系（mach_timebase / cntvct_el0 是否同源）
//   二、读一次时钟的开销（成对读取，扣掉空循环）
//   三、时钟分辨率：连续两次读的最小非零差值与分位数
//   四、批次放大：为什么 0.3 ns 的操作必须打包后再计时

#include <mach/mach_time.h>
#include <time.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

mach_timebase_info_data_t g_tb{};
double g_ns_per_tick = 1.0;

uint64_t read_mach() {
    return mach_absolute_time();
}

#if defined(__aarch64__)
uint64_t read_cntvct() {
    uint64_t v = 0;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}
#endif

uint64_t read_steady_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

uint64_t read_monotonic_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<uint64_t>(ts.tv_nsec);
}

uint64_t read_monotonic_raw_ns() {
#ifdef CLOCK_MONOTONIC_RAW
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<uint64_t>(ts.tv_nsec);
#else
    return read_steady_ns();  // 平台无此时钟源时退化为 steady_clock
#endif
}

uint64_t read_uptime_raw_ns() {
#ifdef CLOCK_UPTIME_RAW
    timespec ts{};
    clock_gettime(CLOCK_UPTIME_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<uint64_t>(ts.tv_nsec);
#else
    return read_steady_ns();  // 平台无此时钟源时退化为 steady_clock
#endif
}

// 空循环基线：用于从读时钟的吞吐里扣掉 for 循环本身的成本。
double loop_baseline_ns(std::size_t n) {
    uint64_t sink = 0;
    const uint64_t a = read_steady_ns();
    for (std::size_t i = 0; i < n; ++i) {
        sink ^= i;
        asm volatile("" ::: "memory");
    }
    const uint64_t b = read_steady_ns();
    asm volatile("" : "+r"(sink));
    return static_cast<double>(b - a) / static_cast<double>(n);
}

// 吞吐法（推荐）：外层只读两次，循环里连续读 N 次，测「每次读」的平均成本。
// 相比成对差值法，外层区间足够长，不受单次时钟分辨率的影响。
template <typename Read>
double throughput_cost_ns(Read read, std::size_t n, double ns_per_unit) {
    for (std::size_t i = 0; i < 10000; ++i) {
        (void)read();  // warmup
    }
    uint64_t sink = 0;
    const uint64_t a = read();
    for (std::size_t i = 0; i < n; ++i) {
        sink ^= read();
        asm volatile("" ::: "memory");
    }
    const uint64_t b = read();
    asm volatile("" : "+r"(sink));
    return static_cast<double>(b - a) * ns_per_unit / static_cast<double>(n);
}

template <typename Read>
void print_resolution(const char* name, Read read, std::size_t n,
                      double ns_per_unit) {
    std::vector<uint64_t> deltas;
    deltas.reserve(n);
    uint64_t prev = read();
    for (std::size_t i = 0; i < n; ++i) {
        uint64_t cur = read();
        deltas.push_back(cur - prev);
        prev = cur;
    }

    std::vector<uint64_t> sorted = deltas;
    std::sort(sorted.begin(), sorted.end());

    std::size_t zeros = 0;
    uint64_t min_nonzero = 0;
    for (uint64_t v : sorted) {
        if (v == 0) {
            ++zeros;
        } else {
            min_nonzero = v;
            break;
        }
    }

    std::vector<uint64_t> uniq = sorted;
    uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());

    auto pct = [&](double p) {
        return sorted[static_cast<std::size_t>(
            p * static_cast<double>(sorted.size() - 1))];
    };

    std::printf("%-24s zero=%5.1f%%  min_nonzero=%7.1f ns  p50=%7.1f  "
                "p99=%8.1f  max=%9.1f  distinct=%zu\n",
                name, 100.0 * static_cast<double>(zeros) /
                          static_cast<double>(n),
                static_cast<double>(min_nonzero) * ns_per_unit,
                static_cast<double>(pct(0.50)) * ns_per_unit,
                static_cast<double>(pct(0.99)) * ns_per_unit,
                static_cast<double>(pct(1.00)) * ns_per_unit, uniq.size());

    std::printf("  first distinct values (ns):");
    for (std::size_t i = 0; i < uniq.size() && i < 6; ++i) {
        std::printf(" %.1f", static_cast<double>(uniq[i]) * ns_per_unit);
    }
    std::printf("\n");
}

#if defined(__aarch64__)
#define DEPENDENT_ADD(x) asm volatile("add %0, %0, #1" : "+r"(x))
#else
#define DEPENDENT_ADD(x) asm volatile("addq $1, %0" : "+r"(x))
#endif

// 一小段依赖加法链，用于演示「单次计时 vs 批次计时」的分辨率差异。
uint64_t dependent_adds(uint64_t x, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        DEPENDENT_ADD(x);
    }
    return x;
}

void experiment_platform() {
    std::printf("[实验一] 平台时钟源与换算关系\n");

    mach_timebase_info(&g_tb);
    g_ns_per_tick = static_cast<double>(g_tb.numer) /
                    static_cast<double>(g_tb.denom);
    std::printf("  mach_timebase: numer=%u denom=%u → 1 tick = %.3f ns "
                "(%.1f MHz)\n",
                g_tb.numer, g_tb.denom, g_ns_per_tick,
                1000.0 / g_ns_per_tick);

#if defined(__aarch64__)
    std::printf("  cntvct_el0 与 mach_absolute_time 的读数差（ticks）:");
    for (int i = 0; i < 8; ++i) {
        uint64_t c = read_cntvct();
        uint64_t m = read_mach();
        std::printf(" %lld", static_cast<long long>(m) -
                                static_cast<long long>(c));
    }
    std::printf("\n");
    std::printf("  （差值恒定说明同频率、不同起点；两个读数不能直接互换）\n");
#endif

    std::printf("  steady_clock period = %lld / %lld s\n",
                static_cast<long long>(std::chrono::steady_clock::period::num),
                static_cast<long long>(std::chrono::steady_clock::period::den));
}

void experiment_overhead() {
    std::printf("\n[实验二] 读一次时钟要多少钱（吞吐法，N=20,000,000）\n");

    constexpr std::size_t kN = 20000000;
    const double loop_ns = loop_baseline_ns(kN);
    std::printf("  空循环基线（含一次 XOR）: %.2f ns/iter\n", loop_ns);

    struct Case {
        const char* name;
        double throughput_ns;
    };

    std::vector<Case> cases;
    cases.push_back(
        {"mach_absolute_time", throughput_cost_ns(read_mach, kN, g_ns_per_tick)});
#if defined(__aarch64__)
    cases.push_back(
        {"cntvct_el0", throughput_cost_ns(read_cntvct, kN, g_ns_per_tick)});
#endif
    cases.push_back({"steady_clock", throughput_cost_ns(read_steady_ns, kN, 1.0)});
    cases.push_back(
        {"clock_gettime(MONOTONIC)",
         throughput_cost_ns(read_monotonic_ns, kN, 1.0)});
    cases.push_back(
        {"clock_gettime(MONOTONIC_RAW)",
         throughput_cost_ns(read_monotonic_raw_ns, kN, 1.0)});
    cases.push_back(
        {"clock_gettime(UPTIME_RAW)",
         throughput_cost_ns(read_uptime_raw_ns, kN, 1.0)});

    for (const Case& c : cases) {
        const double t = c.throughput_ns - loop_ns;
        if (t < 0.1) {
            std::printf("  %-26s throughput≈0（低于空循环基线，无法分辨）\n",
                        c.name);
        } else {
            std::printf("  %-26s throughput=%6.2f ns/read\n", c.name, t);
        }
    }
    std::printf("  注：不要用「两次读的差值」测时钟读取开销——低于一个 tick 只能"
                "得到 0 或 41.7 ns 的量化值。\n");
}

void experiment_resolution() {
    std::printf("\n[实验三] 时钟分辨率：连续两次读的差值分布（N=1,000,000）\n");

    constexpr std::size_t kN = 1000000;
    print_resolution("mach_absolute_time", read_mach, kN, g_ns_per_tick);
#if defined(__aarch64__)
    print_resolution("cntvct_el0", read_cntvct, kN, g_ns_per_tick);
#endif
    print_resolution("steady_clock", read_steady_ns, kN, 1.0);
    print_resolution("clock_gettime(MONOTONIC)", read_monotonic_ns, kN, 1.0);
    print_resolution("clock_gettime(MONOTONIC_RAW)", read_monotonic_raw_ns, kN,
                     1.0);
    print_resolution("clock_gettime(UPTIME_RAW)", read_uptime_raw_ns, kN, 1.0);
}

void experiment_batch() {
    std::printf("\n[实验四] 批次放大：每个样本用两次 steady_clock 读包住 B 条 add\n");
    std::printf("  理想值：M2 Pro 的依赖 add 约 1 cycle/条（约 0.3 ns）\n");

    constexpr std::size_t kTotalOps = 2000000;
    for (std::size_t batch : {std::size_t{1}, std::size_t{10},
                              std::size_t{100}, std::size_t{1000}}) {
        const std::size_t rounds = kTotalOps / batch;
        uint64_t x = 1;
        uint64_t sink = 0;

        x = dependent_adds(x, 10000);  // warmup

        for (std::size_t r = 0; r < rounds; ++r) {
            const uint64_t a = read_steady_ns();
            x = dependent_adds(x, batch);
            const uint64_t b = read_steady_ns();
            sink += b - a;
        }
        asm volatile("" : "+r"(sink), "+r"(x));

        const double per_sample_ns = static_cast<double>(sink) /
                                     static_cast<double>(rounds);
        const double per_op_ns = static_cast<double>(sink) /
                                 static_cast<double>(rounds * batch);
        std::printf("  B=%4zu  rounds=%8zu  每次样本=%8.1f ns  → 单条=%6.2f ns\n",
                    batch, rounds, per_sample_ns, per_op_ns);
    }
    std::printf("  B=1 的样本值含一次时钟读开销；B 越大，开销被摊得越薄。\n");
}

}  // namespace

int main() {
    std::printf("== 计时与计数器：rdtsc / clock_gettime / 性能计数器 ==\n");
    experiment_platform();
    experiment_overhead();
    experiment_resolution();
    experiment_batch();
    return 0;
}
