#pragma once

// 实验公共计时与微架构工具。
//
// 计时说明（与 knowledge/arch/timing-counters 的结论一致）：
// - macOS 上 std::chrono / CLOCK_MONOTONIC 的底层是 mach_absolute_time；
// - Apple Silicon 的计数器频率为 24 MHz，即 41.667 ns/tick，单次事件计时存在
//   约 ±42 ns 的量化误差，因此本实验同时报告总耗时/事件（均值）与 p50/p90。
// - 需要亚纳秒级结论时使用批次计时，见 micro/probe.cpp。

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/qos.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sys/sysinfo.h>
#endif

#if defined(__aarch64__)
// Apple Silicon / ARM64
#elif defined(__x86_64__)
#include <immintrin.h>
#endif

namespace exp {

// ---------------------------------------------------------------------------
// 时间
// ---------------------------------------------------------------------------

inline double ns_per_tick() {
#if defined(__APPLE__)
    static const double v = [] {
        mach_timebase_info_data_t tb{};
        mach_timebase_info(&tb);
        return double(tb.numer) / double(tb.denom);
    }();
    return v;
#else
    return 1.0;
#endif
}

inline uint64_t now_ticks() {
#if defined(__APPLE__)
    return mach_absolute_time();
#else
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
#endif
}

inline uint64_t now_ns() {
#if defined(__APPLE__)
    return uint64_t(double(mach_absolute_time()) * ns_per_tick());
#else
    return now_ticks();
#endif
}

// 返回两次读取之间的纳秒数；调用方应避免在热路径中频繁做字符串/系统调用。
inline uint64_t ticks_to_ns(uint64_t ticks) {
#if defined(__APPLE__)
    return uint64_t(double(ticks) * ns_per_tick());
#else
    return ticks;
#endif
}

// ---------------------------------------------------------------------------
// 线程调度与 CPU 信息
// ---------------------------------------------------------------------------

// 请求 P-core（macOS 的 USER_INTERACTIVE QoS）。macOS 没有硬亲和性 API，
// 该调用只是提示调度器优先放到性能核；实验结果需注明这一点。
inline bool request_performance_core() {
#if defined(__APPLE__)
    return pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0;
#else
    return true;
#endif
}

inline long sysctl_i64(const char* name, long fallback = -1) {
#if defined(__APPLE__)
    int64_t value = 0;
    size_t size = sizeof(value);
    if (sysctlbyname(name, &value, &size, nullptr, 0) != 0) return fallback;
    return long(value);
#else
    (void)name;
    return fallback;
#endif
}

struct CpuInfo {
    const char* model;
    long cores;
    long perf_cores;
    long l1d;
    long l2_per_cluster;
    long line;
};

inline CpuInfo cpu_info() {
    CpuInfo c{};
#if defined(__APPLE__)
    static char model[256] = "unknown";
    size_t size = sizeof(model);
    if (sysctlbyname("machdep.cpu.brand_string", model, &size, nullptr, 0) != 0) {
        std::snprintf(model, sizeof(model), "unknown");
    }
    c.model = model;
    c.cores = sysctl_i64("hw.ncpu", 0);
    c.perf_cores = sysctl_i64("hw.perflevel0.physicalcpu", 0);
    c.l1d = sysctl_i64("hw.perflevel0.l1dcachesize", 0);
    c.l2_per_cluster = sysctl_i64("hw.perflevel0.l2cachesize", 0);
    c.line = sysctl_i64("hw.cachelinesize", 64);
#else
    c.model = "unknown";
    c.cores = 1;
    c.perf_cores = 1;
    c.l1d = 32 * 1024;
    c.l2_per_cluster = 256 * 1024;
    c.line = 64;
#endif
    return c;
}

// ---------------------------------------------------------------------------
// 缓存行操作：x86 与 ARM64 的对应关系
// ---------------------------------------------------------------------------
//
// 原文（Intel x86）       本实验（Apple Silicon ARM64）
// _mm_prefetch(_MM_HINT_T0)      prfm pldl1keep
// _MM_HINT_NTA                   prfm pldl1strm（流式提示，提前驱逐）
// _mm_clflushopt                 dc civac（clean + invalidate to PoC）
// _mm_clwb                       dc cvac（clean to PoC，保留本地副本）
//
// macOS 允许 EL0 执行 dc civac/cvac（由内核陷入模拟），但单条成本远高于
// x86 的 clflushopt/clwb；实验会单独测量这一点，避免把“指令存在”误当成
// “微架构收益存在”。

inline void prefetch_t0(const void* p) {
#if defined(__aarch64__)
    asm volatile("prfm pldl1keep, [%0]" : : "r"(p));
#elif defined(__x86_64__)
    _mm_prefetch(static_cast<const char*>(p), _MM_HINT_T0);
#else
    (void)p;
#endif
}

// 跨核预取：把数据拉到共享 L2，而不是发射核的私有 L1。
inline void prefetch_l2(const void* p) {
#if defined(__aarch64__)
    asm volatile("prfm pldl2keep, [%0]" : : "r"(p));
#elif defined(__x86_64__)
    _mm_prefetch(static_cast<const char*>(p), _MM_HINT_T1);
#else
    (void)p;
#endif
}

inline void prefetch_stream(const void* p) {
#if defined(__aarch64__)
    asm volatile("prfm pldl1strm, [%0]" : : "r"(p));
#elif defined(__x86_64__)
    _mm_prefetch(static_cast<const char*>(p), _MM_HINT_NTA);
#else
    (void)p;
#endif
}

inline void evict_line(const void* p) {
#if defined(__aarch64__)
    asm volatile("dc civac, %0" : : "r"(p) : "memory");
#elif defined(__x86_64__)
    _mm_clflushopt(const_cast<void*>(p));
#else
    (void)p;
#endif
}

inline void publish_line(const void* p) {
#if defined(__aarch64__)
    asm volatile("dc cvac, %0" : : "r"(p) : "memory");
#elif defined(__x86_64__)
    _mm_clwb(const_cast<void*>(p));
#else
    (void)p;
#endif
}

inline void publish_fence() {
#if defined(__aarch64__)
    asm volatile("dsb ish" ::: "memory");
#elif defined(__x86_64__)
    _mm_sfence();
#endif
}

inline void evict_range(void* base, size_t bytes, size_t line) {
    char* p = static_cast<char*>(base);
    for (size_t off = 0; off < bytes; off += line) evict_line(p + off);
    publish_fence();
}

// 真实 load：用于批处理第一步“摸地址”，会占用 ROB 与 Fill Buffer。
inline void touch_line(const void* p) {
    (void)*(volatile const uint64_t*)p;
}

inline void consume_u64(uint64_t v) {
    asm volatile("" : : "r"(v) : "memory");
}

inline void cpu_relax() {
#if defined(__aarch64__)
    asm volatile("yield");
#elif defined(__x86_64__)
    _mm_pause();
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

inline uint64_t load_u64(const void* p) {
    return *static_cast<const volatile uint64_t*>(p);
}

// ---------------------------------------------------------------------------
// 延迟采样与分位数
// ---------------------------------------------------------------------------

class LatencyRecorder {
public:
    explicit LatencyRecorder(size_t capacity) { samples_.reserve(capacity); }

    void clear() { samples_.clear(); }

    void record_since(uint64_t start_ns) { samples_.push_back(now_ns() - start_ns); }

    void record_ns(uint64_t ns) { samples_.push_back(ns); }

    size_t size() const { return samples_.size(); }

    struct Summary {
        uint64_t p50 = 0;
        uint64_t p90 = 0;
        uint64_t p99 = 0;
        double mean = 0.0;
        uint64_t min = 0;
        uint64_t max = 0;
    };

    Summary summarize() {
        Summary s{};
        if (samples_.empty()) return s;
        std::sort(samples_.begin(), samples_.end());
        const size_t n = samples_.size();
        auto at = [&](double q) {
            size_t i = size_t(q * double(n - 1));
            return samples_[i];
        };
        s.p50 = at(0.50);
        s.p90 = at(0.90);
        s.p99 = at(0.99);
        s.min = samples_.front();
        s.max = samples_.back();
        double sum = 0.0;
        for (uint64_t v : samples_) sum += double(v);
        s.mean = sum / double(n);
        return s;
    }

private:
    std::vector<uint64_t> samples_;
};

inline uint64_t median3(uint64_t a, uint64_t b, uint64_t c) {
    if (a > b) std::swap(a, b);
    if (b > c) std::swap(b, c);
    if (a > b) std::swap(a, b);
    return b;
}

} // namespace exp
