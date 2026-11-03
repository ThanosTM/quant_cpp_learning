// example.cpp —— 虚拟内存实验：缺页成本与工作集/TLB 覆盖
// 编译: g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
//       (macOS 可用 clang++ 替换 g++)
//
// 说明：Apple Silicon / macOS 的基础页大小是 16 KB（本机 sysconf 实测），
//       Linux x86 常见 4 KB。实验一按运行时页大小计算，不写死 4 KB。
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <random>
#include <vector>

#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

namespace {

using Clock = std::chrono::steady_clock;

volatile std::uint64_t g_sink = 0;

#if defined(__APPLE__)
constexpr int kMapAnon = MAP_ANON;
#else
constexpr int kMapAnon = MAP_ANONYMOUS;
#endif

std::uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               Clock::now().time_since_epoch())
        .count();
}

// 实验一：首次触页（minor fault）vs 页已驻留后的再次访问
void experiment_page_fault() {
    std::puts("\n[实验一] 缺页成本：首次触页 vs 已驻留");

    constexpr std::size_t kBytes = 64ull << 20;  // 64 MB
    const long page_size = ::sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        std::puts("sysconf(_SC_PAGESIZE) 失败");
        return;
    }
    const std::size_t pages = kBytes / static_cast<std::size_t>(page_size);

    void* raw = ::mmap(nullptr, kBytes, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | kMapAnon, -1, 0);
    if (raw == MAP_FAILED) {
        std::puts("mmap 失败");
        return;
    }

    volatile char* base = static_cast<volatile char*>(raw);

    rusage before{};
    rusage after{};
    ::getrusage(RUSAGE_SELF, &before);
    const std::uint64_t t0 = now_ns();
    for (std::size_t i = 0; i < pages; ++i) {
        base[i * static_cast<std::size_t>(page_size)] = static_cast<char>(i);
    }
    const std::uint64_t t1 = now_ns();
    ::getrusage(RUSAGE_SELF, &after);

    // 第二遍：页已驻留，不再触发 minor fault
    const std::uint64_t t2 = now_ns();
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < pages; ++i) {
        sum += static_cast<std::uint64_t>(
            base[i * static_cast<std::size_t>(page_size)]);
    }
    const std::uint64_t t3 = now_ns();
    g_sink += sum;

    const long faults = after.ru_minflt - before.ru_minflt;
    const double first_ns = static_cast<double>(t1 - t0) / static_cast<double>(pages);
    const double warm_ns = static_cast<double>(t3 - t2) / static_cast<double>(pages);
    const double fault_ns = first_ns - warm_ns;

    std::printf("页大小 = %ld B；缓冲区 = %zu MB；页数 = %zu\n", page_size,
                kBytes >> 20, pages);
    std::printf("首次触页: 总 %.2f ms，约 %.0f ns/页\n",
                static_cast<double>(t1 - t0) / 1e6, first_ns);
    std::printf("再次访问: 总 %.2f ms，约 %.0f ns/页\n",
                static_cast<double>(t3 - t2) / 1e6, warm_ns);
    std::printf("差值 ≈ %.0f ns/页（含缺页处理 + 零页填充）\n", fault_ns);
    std::printf("getrusage: minor faults +%ld（major faults +%ld）\n", faults,
                after.ru_majflt - before.ru_majflt);
    std::puts("  提示：生产热路径上应预触页/锁页，避免第一次访问发生在行情高峰。");

    ::munmap(raw, kBytes);
}

// 实验二：指针追逐，观察工作集从 L1 到 DRAM 时的每次访问延迟
void experiment_tlb_reach() {
    std::puts("\n[实验二] 工作集 / TLB 覆盖：随机指针追逐");

    const long page_size = ::sysconf(_SC_PAGESIZE);
    constexpr std::size_t kSteps = 2'000'000;
    const std::size_t sizes[] = {
        64ull << 10,    // 64 KB
        512ull << 10,   // 512 KB
        4ull << 20,     // 4 MB
        32ull << 20,    // 32 MB
        128ull << 20,   // 128 MB
    };

    std::mt19937_64 rng(20261103);

    for (const std::size_t bytes : sizes) {
        const std::size_t n = bytes / sizeof(std::uint64_t);
        std::vector<std::uint64_t> next(n);

        // 用随机排列构造一条遍历所有元素的指针链；构造完立即释放 perm
        std::uint64_t start = 0;
        {
            std::vector<std::uint32_t> perm(n);
            std::iota(perm.begin(), perm.end(), 0u);
            std::shuffle(perm.begin(), perm.end(), rng);
            for (std::size_t i = 0; i < n; ++i) {
                next[perm[i]] = perm[(i + 1) % n];
            }
            start = perm[0];
        }

        // 预触每页，确保计时阶段不会因构建数组时的缺页而失真
        const std::size_t page_elems =
            static_cast<std::size_t>(page_size) / sizeof(std::uint64_t);
        for (std::size_t i = 0; i < n; i += page_elems) {
            g_sink += next[i];
        }

        // 预热：让链头和部分节点进入缓存
        std::uint64_t cur = start;
        for (std::size_t i = 0; i < 100'000; ++i) cur = next[cur];

        const std::uint64_t t0 = now_ns();
        for (std::size_t i = 0; i < kSteps; ++i) cur = next[cur];
        const std::uint64_t t1 = now_ns();
        g_sink += cur;

        const double ns_per_step =
            static_cast<double>(t1 - t0) / static_cast<double>(kSteps);
        std::printf("%4zu KB: %7.1f ns/步\n", bytes >> 10, ns_per_step);
    }

    std::puts("  说明：工作集越大，L1/L2/L3 与 TLB 逐级失效；");
    std::puts("        随机追逐是依赖加载，测的是延迟而不是带宽。");
}

}  // namespace

int main() {
    std::puts("== 虚拟内存：页表、TLB 与缺页中断 ==");
    std::puts("提示：性能实验受频率、后台负载与热节流影响，建议连跑 3 次取中位数。");

    experiment_page_fault();
    experiment_tlb_reach();

    std::printf("\n(防止优化: sink=%llu)\n",
                static_cast<unsigned long long>(g_sink));
    return 0;
}
