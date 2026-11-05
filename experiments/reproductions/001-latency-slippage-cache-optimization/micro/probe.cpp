// 环境探针：缓存层次延迟、dc civac/cvac 成本、跨核缓存行迁移。
//
// 用法:
//   ./probe all
//   ./probe cache
//   ./probe flush
//   ./probe pingpong

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "../../../common/bench.hpp"

namespace {

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() {
        uint64_t x = s;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        s = x;
        return x;
    }
};

uint64_t now_ticks() { return exp::now_ticks(); }
double ns(uint64_t ticks) { return double(exp::ticks_to_ns(ticks)); }

// 以 128B 行粒度构造随机指针追逐环，测量不同工作集的单次加载延迟。
double chase_latency(size_t bytes, size_t min_iterations = 1'000'000) {
    const size_t line = exp::cpu_info().line;
    const size_t n = bytes / line;
    void* raw = std::aligned_alloc(line, bytes);
    if (!raw) {
        std::fprintf(stderr, "alloc failed for %zu bytes\n", bytes);
        std::exit(1);
    }
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    Rng rng(0x123456789abcdefull + bytes);
    for (size_t i = n - 1; i > 0; --i) {
        size_t j = size_t(rng.next() % (i + 1));
        std::swap(order[i], order[j]);
    }
    char* base = static_cast<char*>(raw);
    for (size_t i = 0; i < n; ++i) {
        char* cur = base + order[i] * line;
        char* next = base + order[(i + 1) % n] * line;
        *reinterpret_cast<uintptr_t*>(cur) = reinterpret_cast<uintptr_t>(next);
    }
    uint64_t* p = reinterpret_cast<uint64_t*>(base + order[0] * line);
    const size_t iters = std::max(min_iterations, n * 8);
    // 预热
    for (size_t i = 0; i < n; ++i) p = reinterpret_cast<uint64_t*>(*p);

    uint64_t t0 = now_ticks();
    for (size_t i = 0; i < iters; ++i) p = reinterpret_cast<uint64_t*>(*p);
    uint64_t t1 = now_ticks();
    exp::consume_u64(reinterpret_cast<uintptr_t>(p));
    std::free(raw);
    return ns(t1 - t0) / double(iters);
}

void cache_report() {
    struct Case { const char* name; size_t bytes; };
    const Case cases[] = {
        {"L1 64KB", 64 * 1024},
        {"L1 128KB", 128 * 1024},
        {"L2 1MB", 1 * 1024 * 1024},
        {"L2 8MB", 8 * 1024 * 1024},
        {"L2 16MB", 16 * 1024 * 1024},
        {"DRAM 32MB", 32 * 1024 * 1024},
        {"DRAM 128MB", 128 * 1024 * 1024},
    };
    std::printf("cache_latency (random pointer chase, ns/load):\n");
    for (const Case& c : cases) {
        double v = chase_latency(c.bytes);
        std::printf("  %-10s %8.2f\n", c.name, v);
    }
}

void flush_report() {
    const size_t line = exp::cpu_info().line;
    char* p = static_cast<char*>(std::aligned_alloc(line, line * 2));
    std::memset(p, 0, line * 2);
    const int N = 1'000'000;

    auto measure = [&](const char* name, auto&& fn) {
        uint64_t t0 = now_ticks();
        for (int i = 0; i < N; ++i) fn(p);
        uint64_t t1 = now_ticks();
        std::printf("  %-18s %8.2f ns/op\n", name, ns(t1 - t0) / N);
    };

    std::printf("cache_op_cost (same line, %d ops):\n", N);
    measure("touch load", [](char* q) { exp::touch_line(q); });
    measure("prfm pldl1keep", [](char* q) { exp::prefetch_t0(q); });
    measure("dc civac", [](char* q) { exp::evict_line(q); });
    measure("dc cvac", [](char* q) { exp::publish_line(q); });
    std::free(p);
}

struct alignas(128) PingShared {
    std::atomic<uint64_t> turn{0};
    char pad0[128 - 8];
    alignas(128) uint64_t payload[16];
};

void pingpong_report(bool publish) {
    PingShared shared{};
    const int N = 500'000;
    uint64_t sink = 0;

    std::thread b([&] {
        exp::request_performance_core();
        for (int i = 0; i < N; ++i) {
            while (shared.turn.load(std::memory_order_acquire) != 1) exp::cpu_relax();
            uint64_t sum = 0;
            for (int k = 0; k < 16; ++k) sum += shared.payload[k];
            sink += sum;
            shared.turn.store(0, std::memory_order_release);
        }
    });

    exp::request_performance_core();
    uint64_t t0 = now_ticks();
    for (int i = 0; i < N; ++i) {
        while (shared.turn.load(std::memory_order_acquire) != 0) exp::cpu_relax();
        for (int k = 0; k < 16; ++k) shared.payload[k] = uint64_t(i + k);
        if (publish) {
            exp::publish_line(shared.payload);
            exp::publish_fence();
        }
        shared.turn.store(1, std::memory_order_release);
    }
    b.join();
    uint64_t t1 = now_ticks();
    exp::consume_u64(sink);

    const double per_handoff = ns(t1 - t0) / N;
    std::printf("  %-22s %8.2f ns/handoff  (%.2f ns/round-trip)\n",
                publish ? "dirty + dc cvac" : "dirty (no cvac)", per_handoff,
                per_handoff * 2.0);
}

} // namespace

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "all";
    const exp::CpuInfo cpu = exp::cpu_info();
    std::printf("cpu=%s cores=%ld perf_cores=%ld l1d=%ld l2_cluster=%ld line=%ld\n",
                cpu.model, cpu.cores, cpu.perf_cores, cpu.l1d, cpu.l2_per_cluster,
                cpu.line);
    exp::request_performance_core();

    if (std::strcmp(mode, "all") == 0 || std::strcmp(mode, "cache") == 0) cache_report();
    if (std::strcmp(mode, "all") == 0 || std::strcmp(mode, "flush") == 0) flush_report();
    if (std::strcmp(mode, "all") == 0 || std::strcmp(mode, "pingpong") == 0) {
        std::printf("cross_core_transfer (128B payload):\n");
        pingpong_report(false);
        pingpong_report(true);
    }
    return 0;
}
