#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../../../common/bench.hpp"
#include "case.hpp"

namespace {

using namespace ob;

struct Stats {
    exp::LatencyRecorder::Summary latency;
    Check check;
};

template <class Book>
Stats run_book(Book& book, const std::vector<Event>& events, int batch, int warmup,
               HeapPolluter* polluter = nullptr, bool churn_inside = false) {
    const int n = int(events.size());
    const int begin = std::min(warmup, std::max(1, n / 10));
    for (int i = 0; i < begin; ++i) {
        book.apply(events[size_t(i)]);
        if (polluter && churn_inside) polluter->churn(1);
    }

    exp::LatencyRecorder rec(size_t((n - begin) / batch + 1));
    for (int i = begin; i + batch <= n; i += batch) {
        const uint64_t t0 = exp::now_ticks();
        for (int k = 0; k < batch; ++k) {
            book.apply(events[size_t(i + k)]);
            // 模拟“操作之间还在分配/释放内存”的真实环境；开销计入延迟。
            if (polluter && churn_inside) polluter->churn(1);
        }
        const uint64_t dt = exp::now_ticks() - t0;
        rec.record_ns(exp::ticks_to_ns(dt) / uint64_t(batch));
    }
    for (int i = begin + ((n - begin) / batch) * batch; i < n; ++i) {
        book.apply(events[size_t(i)]);
    }

    Stats s{};
    s.latency = rec.summarize();
    s.check = book.check();
    return s;
}

template <class Book>
Check run_full(Book& book, const std::vector<Event>& events) {
    for (const Event& e : events) book.apply(e);
    return book.check();
}

const char* kCases[] = {
    "map-clean", "map-polluted", "vector-front", "vector-back",
    "binary-branchy", "binary-branchless", "linear",
};

bool valid_case(const char* name) {
    for (const char* c : kCases) {
        if (std::strcmp(c, name) == 0) return true;
    }
    return false;
}

Stats run_case(const std::string& name, const std::vector<Event>& events, int max_levels,
               int batch, int warmup) {
    if (name == "map-clean") {
        MapBook book(max_levels);
        return run_book(book, events, batch, warmup);
    }
    if (name == "map-polluted") {
        HeapPolluter polluter;
        MapBook book(max_levels);
        return run_book(book, events, batch, warmup, &polluter, true);
    }
    if (name == "vector-front") {
        VectorBook<Layout::Descending, Search::Std> book(max_levels);
        return run_book(book, events, batch, warmup);
    }
    if (name == "vector-back") {
        VectorBook<Layout::Ascending, Search::Std> book(max_levels);
        return run_book(book, events, batch, warmup);
    }
    if (name == "binary-branchy") {
        VectorBook<Layout::Ascending, Search::Branchy> book(max_levels);
        return run_book(book, events, batch, warmup);
    }
    if (name == "binary-branchless") {
        VectorBook<Layout::Ascending, Search::Branchless> book(max_levels);
        return run_book(book, events, batch, warmup);
    }
    if (name == "linear") {
        VectorBook<Layout::Ascending, Search::Linear> book(max_levels);
        return run_book(book, events, batch, warmup);
    }
    std::fprintf(stderr, "unknown case: %s\n", name.c_str());
    std::exit(2);
}

Check run_case_full(const std::string& name, const std::vector<Event>& events, int max_levels) {
    if (name == "map-clean") {
        MapBook book(max_levels);
        return run_full(book, events);
    }
    if (name == "map-polluted") {
        HeapPolluter polluter;
        MapBook book(max_levels);
        return run_full(book, events);
    }
    if (name == "vector-front") {
        VectorBook<Layout::Descending, Search::Std> book(max_levels);
        return run_full(book, events);
    }
    if (name == "vector-back") {
        VectorBook<Layout::Ascending, Search::Std> book(max_levels);
        return run_full(book, events);
    }
    if (name == "binary-branchy") {
        VectorBook<Layout::Ascending, Search::Branchy> book(max_levels);
        return run_full(book, events);
    }
    if (name == "binary-branchless") {
        VectorBook<Layout::Ascending, Search::Branchless> book(max_levels);
        return run_full(book, events);
    }
    if (name == "linear") {
        VectorBook<Layout::Ascending, Search::Linear> book(max_levels);
        return run_full(book, events);
    }
    std::fprintf(stderr, "unknown case: %s\n", name.c_str());
    std::exit(2);
}

int run_verify(const std::vector<Event>& events, int max_levels) {
    Check reference{};
    bool ok = true;
    for (const char* name : kCases) {
        const Check c = run_case_full(name, events, max_levels);
        if (std::strcmp(name, "map-clean") == 0) reference = c;
        std::printf("verify %-16s count=%llu sum=%lld mix=0x%llx", name,
                    (unsigned long long)c.count, (long long)c.sum,
                    (unsigned long long)c.mix);
        if (!(c == reference)) {
            std::printf("  MISMATCH");
            ok = false;
        }
        std::printf("\n");
    }
    std::printf("%s: all order-book structures produce identical state\n",
                ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

void print_stats(const std::string& name, uint32_t ops, uint32_t max_levels, int batch,
                 const Stats& s) {
    std::printf("case=%s ops=%u max_levels=%u batch=%d\n", name.c_str(), ops, max_levels,
                batch);
    std::printf("  p50=%llu ns  p90=%llu ns  p99=%llu ns  mean=%.1f ns  min=%llu  max=%llu\n",
                (unsigned long long)s.latency.p50, (unsigned long long)s.latency.p90,
                (unsigned long long)s.latency.p99, s.latency.mean,
                (unsigned long long)s.latency.min, (unsigned long long)s.latency.max);
    std::printf("  checksum: count=%llu sum=%lld mix=0x%llx\n",
                (unsigned long long)s.check.count, (long long)s.check.sum,
                (unsigned long long)s.check.mix);
    std::printf("CSV,%s,%u,%u,%d,%llu,%llu,%llu,%.1f,%llu,%llu,%llu,%lld,%llu\n",
                name.c_str(), ops, max_levels, batch,
                (unsigned long long)s.latency.p50, (unsigned long long)s.latency.p90,
                (unsigned long long)s.latency.p99, s.latency.mean,
                (unsigned long long)s.latency.min, (unsigned long long)s.latency.max,
                (unsigned long long)s.check.count, (long long)s.check.sum,
                (unsigned long long)s.check.mix);
}

void usage(const char* argv0) {
    std::printf(
        "usage:\n"
        "  %s --gen-events FILE [--ops N] [--max-levels M]\n"
        "  %s --events FILE --case NAME [--batch N]\n"
        "  %s --events FILE --verify\n"
        "cases: map-clean map-polluted vector-front vector-back binary-branchy\n"
        "       binary-branchless linear\n",
        argv0, argv0, argv0);
}

} // namespace

int main(int argc, char** argv) {
    std::string gen_path;
    std::string events_path;
    std::string case_name = "map-clean";
    uint32_t ops = 2'000'000;
    uint32_t max_levels = 256;
    int batch = 64;
    int warmup = 100'000;
    bool verify = false;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                usage(argv[0]);
                std::exit(2);
            }
            return argv[++i];
        };
        if (std::strcmp(a, "--gen-events") == 0) {
            gen_path = next();
        } else if (std::strcmp(a, "--events") == 0) {
            events_path = next();
        } else if (std::strcmp(a, "--case") == 0) {
            case_name = next();
        } else if (std::strcmp(a, "--ops") == 0) {
            ops = uint32_t(std::strtoul(next(), nullptr, 10));
        } else if (std::strcmp(a, "--max-levels") == 0) {
            max_levels = uint32_t(std::strtoul(next(), nullptr, 10));
        } else if (std::strcmp(a, "--batch") == 0) {
            batch = std::atoi(next());
        } else if (std::strcmp(a, "--warmup") == 0) {
            warmup = std::atoi(next());
        } else if (std::strcmp(a, "--verify") == 0) {
            verify = true;
        } else if (std::strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    const exp::CpuInfo cpu = exp::cpu_info();
    std::fprintf(stderr, "cpu=%s cores=%ld perf_cores=%ld l1d=%ld l2_cluster=%ld line=%ld\n",
                 cpu.model, cpu.cores, cpu.perf_cores, cpu.l1d, cpu.l2_per_cluster, cpu.line);
    exp::request_performance_core();

    if (!gen_path.empty()) {
        if (ops == 0 || max_levels == 0) {
            std::fprintf(stderr, "ops and max-levels must be positive\n");
            return 2;
        }
        if (!write_events(gen_path.c_str(), ops, max_levels)) {
            std::fprintf(stderr, "failed to write %s\n", gen_path.c_str());
            return 1;
        }
        std::printf("generated %u events, max_levels=%u -> %s\n", ops, max_levels,
                    gen_path.c_str());
        return 0;
    }

    if (events_path.empty()) {
        usage(argv[0]);
        return 2;
    }
    std::vector<Event> events;
    uint32_t file_max_levels = 0;
    if (!load_events(events_path.c_str(), events, file_max_levels)) {
        std::fprintf(stderr, "failed to load %s\n", events_path.c_str());
        return 1;
    }
    if (max_levels == 0) max_levels = file_max_levels;
    if (batch < 1 || batch > 4096) {
        std::fprintf(stderr, "batch must be in [1,4096]\n");
        return 2;
    }

    if (verify) return run_verify(events, int(file_max_levels));
    if (!valid_case(case_name.c_str())) {
        std::fprintf(stderr, "unknown case: %s\n", case_name.c_str());
        return 2;
    }

    const Stats s = run_case(case_name, events, int(file_max_levels), batch, warmup);
    print_stats(case_name, uint32_t(events.size()), file_max_levels, batch, s);
    return 0;
}
