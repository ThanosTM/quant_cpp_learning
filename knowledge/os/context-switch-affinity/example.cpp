// example.cpp —— OS 低延迟基础实验：系统调用/vDSO、线程切换、睡眠抖动、CPU 亲和性
// 编译: g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
//       (macOS 可用 clang++ 替换 g++)
//
// 说明：在 Apple Silicon / macOS 上测得的是 Darwin 的量级；Linux 内核路径不同，绝对值会变，
//       但结论一致：用户态读时钟远快于系统调用；线程切换是微秒级；定时唤醒的抖动远大于睡眠本身。
#if defined(__linux__)
#define _GNU_SOURCE
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <thread>
#include <vector>

#include <unistd.h>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

// 防止编译器把“无副作用”的调用/循环优化掉
volatile std::uint64_t g_sink = 0;

template <typename F>
double bench_ns_per_call(const char* name, std::uint64_t iters, F&& f) {
    // 预热，减小首次调用的冷启动噪声
    for (std::uint64_t i = 0; i < 10'000; ++i) f();

    const auto t0 = Clock::now();
    for (std::uint64_t i = 0; i < iters; ++i) f();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        Clock::now() - t0)
                        .count();

    const double per = static_cast<double>(ns) / static_cast<double>(iters);
    std::printf("%-24s %8.1f ns/call  (%llu 次，总 %.2f ms)\n", name, per,
                static_cast<unsigned long long>(iters),
                static_cast<double>(ns) / 1e6);
    return per;
}

// 实验一：空循环 vs 系统调用 vs vDSO/commpage 时钟
void experiment_syscall_vs_vdso() {
    std::puts("\n[实验一] 系统调用 vs 用户态时钟读取");

    constexpr std::uint64_t kIters = 1'000'000;

    const double loop_ns = bench_ns_per_call("空循环基线", kIters, [] {
        std::uint64_t x = 1;
        asm volatile("" : "+r"(x));  // 空操作，仅阻止优化
        g_sink += x;
    });

    const double getpid_ns = bench_ns_per_call("getpid() libc 调用", kIters, [] {
        g_sink += static_cast<std::uint64_t>(::getpid());
    });

    // fcntl(F_GETFD)：不做实际 I/O、不分配资源，是测量最小系统调用成本的干净探针
    const double raw_ns = bench_ns_per_call("fcntl(F_GETFD)", kIters, [] {
        g_sink += static_cast<std::uint64_t>(::fcntl(STDOUT_FILENO, F_GETFD));
    });

    const double clock_ns = bench_ns_per_call("clock_gettime()", kIters, [] {
        timespec ts{};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        g_sink += static_cast<std::uint64_t>(ts.tv_nsec);
    });

    const double sys_overhead = raw_ns - loop_ns;
    const double clock_overhead = clock_ns - loop_ns;
    std::printf("→ 扣除空循环后：原生系统调用 ≈ %.1f ns，读时钟 ≈ %.1f ns（差 %.1fx）\n",
                sys_overhead, clock_overhead,
                clock_overhead > 0 ? sys_overhead / clock_overhead : 0.0);
    std::printf("  注意：getpid() 仅 %.1f ns，因为 macOS libc 缓存了 PID，并未真正进内核；"
                "这也说明“函数名像系统调用”不等于真的发生系统调用。\n", getpid_ns);
    std::puts("  说明：clock_gettime 在 Linux 上走 vDSO、在 macOS 上走 commpage，不进入内核。");
}

// 实验二：两个线程通过管道来回传递 token，强制发生阻塞与唤醒
// 一个往返 = 2 次上下文切换 + 4 次系统调用（write/read 各两次）
std::uint64_t pingpong_once(std::uint64_t rounds) {
    int a2b[2];
    int b2a[2];
    if (::pipe(a2b) != 0 || ::pipe(b2a) != 0) {
        std::puts("pipe 创建失败");
        return 0;
    }

    char token = 0;
    std::thread worker([&] {
        for (std::uint64_t i = 0; i < rounds; ++i) {
            if (::read(a2b[0], &token, 1) != 1) break;
            if (::write(b2a[1], &token, 1) != 1) break;
        }
    });

    const auto t0 = Clock::now();
    for (std::uint64_t i = 0; i < rounds; ++i) {
        if (::write(a2b[1], &token, 1) != 1) break;
        if (::read(b2a[0], &token, 1) != 1) break;
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        Clock::now() - t0)
                        .count();

    worker.join();
    ::close(a2b[0]);
    ::close(a2b[1]);
    ::close(b2a[0]);
    ::close(b2a[1]);
    return static_cast<std::uint64_t>(ns);
}

void experiment_context_switch() {
    std::puts("\n[实验二] 线程上下文切换（管道 ping-pong）");

    constexpr std::uint64_t kRounds = 50'000;
    (void)pingpong_once(10'000);  // 预热：让调度器/缓存进入稳定状态

    const std::uint64_t ns = pingpong_once(kRounds);
    const double per_roundtrip = static_cast<double>(ns) / static_cast<double>(kRounds);
    const double per_switch = per_roundtrip / 2.0;
    std::printf("往返延迟 ≈ %.0f ns；折合每次切换 ≈ %.0f ns（含 syscall，非纯切换成本）\n",
                per_roundtrip, per_switch);
    std::printf("吞吐 ≈ %.0f 万次往返/秒\n", 1e9 / per_roundtrip / 1e4);
}

// 实验三：nanosleep 的唤醒抖动（尾延迟视角）
void experiment_sleep_jitter() {
    std::puts("\n[实验三] 定时唤醒抖动（nanosleep 1 ms）");

    constexpr int kSamples = 500;
    constexpr long kSleepNs = 1'000'000;  // 1 ms
    std::vector<double> overshoot_us;
    overshoot_us.reserve(kSamples);

    timespec req{};
    req.tv_sec = 0;
    req.tv_nsec = kSleepNs;

    for (int i = 0; i < 10; ++i) ::nanosleep(&req, nullptr);  // 预热

    for (int i = 0; i < kSamples; ++i) {
        const auto t0 = Clock::now();
        ::nanosleep(&req, nullptr);
        const auto actual_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0)
                .count();
        overshoot_us.push_back(static_cast<double>(actual_ns - kSleepNs) / 1e3);
    }

    std::sort(overshoot_us.begin(), overshoot_us.end());
    const double p50 = overshoot_us[static_cast<std::size_t>(kSamples * 0.50)];
    const double p99 = overshoot_us[static_cast<std::size_t>(kSamples * 0.99)];
    const double max = overshoot_us.back();
    std::printf("唤醒超时（overshoot）：p50 = %.1f us，p99 = %.1f us，max = %.1f us\n",
                p50, p99, max);
    std::puts("  说明：睡眠 1 ms 的请求，真正唤醒往往晚几十到几百微秒；尾延迟由调度器、"
              "定时器合并（timer coalescing）与 C-state 共同决定。");
}

// 实验四：CPU 亲和性（Linux 硬绑核；macOS 只提供提示性 API）
void experiment_affinity() {
    std::puts("\n[实验四] CPU 亲和性");

#if defined(__linux__)
    pthread_t self = ::pthread_self();
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(3, &set);  // 示例：绑定到 CPU 3（生产上应选隔离核，而不是 0 号核）
    const int rc = ::pthread_setaffinity_np(self, sizeof(set), &set);
    if (rc == 0) {
        std::printf("pthread_setaffinity_np(CPU 3) 成功，当前运行在 CPU %d\n",
                    ::sched_getcpu());
    } else {
        std::printf("pthread_setaffinity_np 失败，rc=%d（需要相应权限/CPU 存在）\n", rc);
    }
#else
    std::puts("当前平台无硬亲和性 API：macOS 的 THREAD_AFFINITY_POLICY 只是提示，"
              "不能像 Linux 的 taskset/pthread_setaffinity_np 那样强制绑核。");
#endif
    std::puts("  Linux 等价命令: taskset -c 3 ./example / chrt -f 90 ./example");
    std::puts("  隔离核还需配合 isolcpus=、nohz_full=、rcu_nocbs= 与 IRQ 亲和性。");
}

}  // namespace

int main() {
    std::puts("== 上下文切换、调度与 CPU 亲和性 ==");
    std::puts("提示：性能实验受频率、后台负载与热节流影响，建议连跑 3 次取中位数。");

    experiment_syscall_vs_vdso();
    experiment_context_switch();
    experiment_sleep_jitter();
    experiment_affinity();

    std::printf("\n(防止优化: sink=%llu)\n",
                static_cast<unsigned long long>(g_sink));
    return 0;
}
