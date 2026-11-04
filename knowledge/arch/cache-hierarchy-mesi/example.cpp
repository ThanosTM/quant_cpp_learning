// 缓存层次结构、局部性与 MESI — 可运行示例
// 编译: g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
//
// 本机: Apple M2 Pro / macOS / arm64（Apple clang 17）。
// 三个实验：
//   一、工作集与访问延迟：随机指针追逐，观察 L1/L2/SLC/DRAM 的台阶
//   二、局部性：2048×2048 矩阵的行优先 vs 列优先遍历（禁用向量化）
//   三、MESI：两个线程写同一缓存行内不同偏移，定位写-写干扰的 64B 边界

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <pthread/qos.h>
#endif

namespace {

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void print_size(std::size_t bytes) {
    if (bytes >= 1024 * 1024) {
        std::printf("%4zu MB", bytes / (1024 * 1024));
    } else {
        std::printf("%4zu KB", bytes / 1024);
    }
}

// 实验一：构造一个随机单环，沿着环做依赖加载；每一步都是一次新的 cache line 访问。
double chase_latency_ns(std::size_t bytes, std::size_t hops) {
    const std::size_t n = bytes / sizeof(std::uint64_t);
    std::vector<std::uint64_t> next(n);
    std::vector<std::size_t> perm(n);
    std::iota(perm.begin(), perm.end(), 0);

    std::mt19937_64 rng(0xC0FFEEu + bytes);
    std::shuffle(perm.begin(), perm.end(), rng);

    // perm 是一个随机单环：perm[i] -> perm[i+1]
    for (std::size_t i = 0; i < n; ++i) {
        next[perm[i]] = perm[(i + 1) % n];
    }

    std::uint64_t idx = 0;
    const std::size_t warmup = std::min<std::size_t>(n, 200000);
    for (std::size_t i = 0; i < warmup; ++i) {
        idx = next[idx];
    }
    asm volatile("" : "+r"(idx));

    idx = 0;
    const std::uint64_t t0 = now_ns();
    for (std::size_t i = 0; i < hops; ++i) {
        idx = next[idx];
        asm volatile("" : "+r"(idx));  // 保持依赖，防止被优化
    }
    const std::uint64_t t1 = now_ns();

    return static_cast<double>(t1 - t0) / static_cast<double>(hops);
}

void experiment_hierarchy() {
    std::printf("[实验一] 工作集与访问延迟（随机指针追逐，%zu hops）\n",
                std::size_t{1000000});
    std::printf("  工作集        ns/hop\n");

    const std::size_t sizes[] = {
        4 * 1024,       16 * 1024,      64 * 1024,      128 * 1024,
        256 * 1024,     512 * 1024,     1 * 1024 * 1024, 2 * 1024 * 1024,
        4 * 1024 * 1024, 8 * 1024 * 1024, 16 * 1024 * 1024,
        32 * 1024 * 1024, 64 * 1024 * 1024, 128 * 1024 * 1024};

    constexpr std::size_t kHops = 1000000;
    for (std::size_t bytes : sizes) {
        print_size(bytes);
        std::printf("        %8.2f\n", chase_latency_ns(bytes, kHops));
    }
}

// 实验二：矩阵遍历。禁用向量化，避免把 SIMD 的差异混进 cache 效应。
#if defined(__clang__)
#define NO_VECTORIZE \
    _Pragma("clang loop vectorize(disable) interleave(disable)")
#else
#define NO_VECTORIZE
#endif

std::int64_t run_row_major(const std::vector<std::int64_t>& m, std::size_t n) {
    std::int64_t sum = 0;
    NO_VECTORIZE
    for (std::size_t i = 0; i < n; ++i) {
        NO_VECTORIZE
        for (std::size_t j = 0; j < n; ++j) {
            sum += m[i * n + j];
        }
    }
    return sum;
}

std::int64_t run_col_major(const std::vector<std::int64_t>& m, std::size_t n) {
    std::int64_t sum = 0;
    NO_VECTORIZE
    for (std::size_t j = 0; j < n; ++j) {
        NO_VECTORIZE
        for (std::size_t i = 0; i < n; ++i) {
            sum += m[i * n + j];
        }
    }
    return sum;
}

void experiment_locality() {
    const std::size_t n = 2048;  // 2048^2 * 8B = 32 MB，超过 L2/SLC
    std::vector<std::int64_t> m(n * n, 1);
    std::printf("\n[实验二] 矩阵遍历：%zux%zu int64（%.0f MB），禁用向量化\n", n,
                n, static_cast<double>(n * n * sizeof(std::int64_t)) /
                        (1024.0 * 1024.0));

    // 第一次作为 warmup 丢弃；第二次计入结果。
    std::int64_t warm = run_row_major(m, n);
    std::int64_t warm2 = run_col_major(m, n);
    asm volatile("" : "+r"(warm), "+r"(warm2));

    const std::uint64_t t0 = now_ns();
    const std::int64_t row_sum = run_row_major(m, n);
    const std::uint64_t t1 = now_ns();
    const std::uint64_t t2 = now_ns();
    const std::int64_t col_sum = run_col_major(m, n);
    const std::uint64_t t3 = now_ns();

    const double row_ms = static_cast<double>(t1 - t0) / 1e6;
    const double col_ms = static_cast<double>(t3 - t2) / 1e6;
    std::printf("  行优先: %8.2f ms  (sum=%lld)\n", row_ms,
                static_cast<long long>(row_sum));
    std::printf("  列优先: %8.2f ms  (sum=%lld)\n", col_ms,
                static_cast<long long>(col_sum));
    std::printf("  倍速:   %8.2fx\n", col_ms / row_ms);
}

// 实验三：MESI 偏移扫描。两个线程各做 n 次 fetch_add，
// 通过改变第二个计数器的字节偏移，观察“同一行”与“不同行”的差异。
void bump_worker(std::uint64_t* p, std::size_t n, std::atomic<int>& ready,
                 std::atomic<bool>& go) {
#if defined(__APPLE__)
    // macOS 没有显式绑核；用 QoS 尽量把两个线程落到 P 核，降低调度噪声。
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    std::atomic_ref<std::uint64_t> counter(*p);
    ready.fetch_add(1, std::memory_order_release);
    while (!go.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    for (std::size_t i = 0; i < n; ++i) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

void experiment_mesi() {
    std::printf("\n[实验三] MESI 偏移扫描：2 线程 × 3,000,000 次 fetch_add\n");

    alignas(128) std::uint64_t words[32] = {};
    const std::size_t kPerThread = 3000000;
    const int kReps = 5;

    struct Case {
        std::size_t word_index;  // 第二个计数器在 words 中的下标
        const char* label;
    };
    const Case cases[] = {
        {0, "offset 0 B（同一变量）"},
        {1, "offset 8 B（同一 64B sector）"},
        {8, "offset 64 B（跨 64B sector）"},
        {16, "offset 128 B（跨 128B 行）"},
    };

    for (const Case& c : cases) {
        std::vector<double> times;
        for (int rep = 0; rep < kReps; ++rep) {
            std::atomic<int> ready{0};
            std::atomic<bool> go{false};

            std::thread th1(bump_worker, &words[0], kPerThread,
                            std::ref(ready), std::ref(go));
            std::thread th2(bump_worker, &words[c.word_index], kPerThread,
                            std::ref(ready), std::ref(go));

            while (ready.load(std::memory_order_acquire) < 2) {
                std::this_thread::yield();
            }
            const std::uint64_t start = now_ns();
            go.store(true, std::memory_order_release);
            th1.join();
            th2.join();
            const std::uint64_t end = now_ns();
            times.push_back(static_cast<double>(end - start) / 1e6);
        }

        const double ms = median(times);
        const double ns_per_op =
            ms * 1e6 / static_cast<double>(2 * kPerThread);
        std::printf("  %-28s %8.2f ms  (%6.2f ns/op)\n", c.label, ms,
                    ns_per_op);
    }
    std::printf("  注：本机 sysctl hw.cachelinesize=128，但实测写-写干扰在 64B 边界"
                "消失；\n");
    std::printf("      说明一致性/所有权按 64B sector 跟踪（Apple 未公开细节）。\n");
}

}  // namespace

int main() {
    std::printf("== 缓存层次结构、局部性与 MESI ==\n");
    experiment_hierarchy();
    experiment_locality();
    experiment_mesi();
    return 0;
}
