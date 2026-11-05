#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../../../common/bench.hpp"
#include "case.hpp"
#include "stage0_baseline.hpp"
#include "stage1_batch.hpp"
#include "stage2_prefetch.hpp"
#include "stage3_evict.hpp"
#include "stage4_dual_core.hpp"

namespace {

using namespace case001;

struct Config {
    std::string stage = "0";
    int batch = 8;
    PrefetchHint hint = PrefetchHint::T0;
    int distance = 1;
    int split = 48;
    uint64_t seed = 20261105ull;
};

void usage(const char* argv0) {
    std::printf(
        "usage: %s [--stage 0|1|2|3|4a|4b|4c|verify] [--batch D] [--hint t0|stream]\n"
        "          [--distance N] [--split N] [--seed N]\n"
        "  stage 0  baseline\n"
        "  stage 1  batch (D options at a time)\n"
        "  stage 2  batch + software prefetch\n"
        "  stage 3  stage 2 + event-line eviction\n"
        "  stage 4a dual core, full results copied back\n"
        "  stage 4b dual core, decoupled hit list\n"
        "  stage 4c stage 4b + dc cvac publish\n",
        argv0);
}

const char* hint_name(PrefetchHint h) {
    return h == PrefetchHint::T0 ? "t0" : "stream";
}

const char* dual_name(DualMode m) {
    switch (m) {
        case DualMode::FullResults: return "full";
        case DualMode::Compact: return "compact";
        case DualMode::CompactPublish: return "compact+publish";
    }
    return "?";
}

void print_result(const Config& cfg, const exp::LatencyRecorder::Summary& s,
                  const Result& r, const char* extra = "") {
    std::printf(
        "stage=%s batch=%d hint=%s distance=%d %s\n"
        "  p50=%llu ns  p90=%llu ns  p99=%llu ns  mean=%.1f ns  min=%llu  max=%llu\n"
        "  hits=%llu qsum=%lld mix=0x%llx events=%d\n",
        cfg.stage.c_str(), cfg.batch, hint_name(cfg.hint), cfg.distance, extra,
        (unsigned long long)s.p50, (unsigned long long)s.p90, (unsigned long long)s.p99,
        s.mean, (unsigned long long)s.min, (unsigned long long)s.max,
        (unsigned long long)r.hits, (long long)r.qsum, (unsigned long long)r.mix, kEvents);
}

void print_csv(const Config& cfg, const exp::LatencyRecorder::Summary& s, const Result& r) {
    std::printf(
        "CSV,%s,%d,%s,%d,%d,%llu,%llu,%llu,%.1f,%llu,%llu,%llu,%lld,%llu\n",
        cfg.stage.c_str(), cfg.batch, hint_name(cfg.hint), cfg.distance, kEvents,
        (unsigned long long)s.p50, (unsigned long long)s.p90, (unsigned long long)s.p99,
        s.mean, (unsigned long long)s.min, (unsigned long long)s.max,
        (unsigned long long)r.hits, (long long)r.qsum, (unsigned long long)r.mix);
}

bool same_result(const Result& a, const Result& b) {
    return a.hits == b.hits && a.qsum == b.qsum && a.mix == b.mix;
}

int run_verify(const Config& cfg) {
    struct Named {
        const char* name;
        Result result;
    };
    std::vector<Named> results;
    const int cap = kEvents - kWarmup;

    auto add = [&](const char* name, Result r) {
        std::printf("verify %-12s hits=%llu qsum=%lld mix=0x%llx\n", name,
                    (unsigned long long)r.hits, (long long)r.qsum,
                    (unsigned long long)r.mix);
        results.push_back({name, r});
    };

    {
        World w = make_world(cfg.seed);
        exp::LatencyRecorder rec{size_t(cap)};
        add("baseline", run_baseline(w, rec));
    }
    {
        World w = make_world(cfg.seed);
        exp::LatencyRecorder rec{size_t(cap)};
        add("batch4", run_batch(w, rec, 4));
    }
    {
        World w = make_world(cfg.seed);
        exp::LatencyRecorder rec{size_t(cap)};
        add("prefetch", run_prefetch(w, rec, 4, PrefetchHint::T0, 1));
    }
    {
        World w = make_world(cfg.seed);
        exp::LatencyRecorder rec{size_t(cap)};
        add("evict", run_evict(w, rec, 4, PrefetchHint::T0, 1));
    }
    {
        World w = make_world(cfg.seed);
        exp::LatencyRecorder rec{size_t(cap)};
        add("dual_full", run_dual_core(w, rec, DualMode::FullResults));
    }
    {
        World w = make_world(cfg.seed);
        exp::LatencyRecorder rec{size_t(cap)};
        add("dual_compact", run_dual_core(w, rec, DualMode::Compact));
    }
    {
        World w = make_world(cfg.seed);
        exp::LatencyRecorder rec{size_t(cap)};
        add("dual_publish", run_dual_core(w, rec, DualMode::CompactPublish));
    }

    bool ok = true;
    for (size_t i = 1; i < results.size(); ++i) {
        if (!same_result(results[0].result, results[i].result)) {
            std::printf("MISMATCH: %s vs %s\n", results[0].name, results[i].name);
            ok = false;
        }
    }
    std::printf("%s: all stages produce identical hit/qsum/mix\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                usage(argv[0]);
                std::exit(2);
            }
            return argv[++i];
        };
        if (std::strcmp(a, "--stage") == 0) {
            cfg.stage = next();
        } else if (std::strcmp(a, "--batch") == 0) {
            cfg.batch = std::atoi(next());
        } else if (std::strcmp(a, "--hint") == 0) {
            const char* v = next();
            cfg.hint = std::strcmp(v, "stream") == 0 ? PrefetchHint::Stream : PrefetchHint::T0;
        } else if (std::strcmp(a, "--distance") == 0) {
            cfg.distance = std::atoi(next());
        } else if (std::strcmp(a, "--split") == 0) {
            cfg.split = std::atoi(next());
        } else if (std::strcmp(a, "--seed") == 0) {
            cfg.seed = std::strtoull(next(), nullptr, 10);
        } else if (std::strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (cfg.batch < 1 || cfg.batch > 64 || (64 % cfg.batch) != 0) {
        std::fprintf(stderr, "batch must divide 64, got %d\n", cfg.batch);
        return 2;
    }
    if (cfg.distance < 0 || cfg.distance > 8) {
        std::fprintf(stderr, "distance must be in [0,8], got %d\n", cfg.distance);
        return 2;
    }

    const exp::CpuInfo cpu = exp::cpu_info();
    std::fprintf(stderr,
                 "cpu=%s cores=%ld perf_cores=%ld l1d=%ld l2_cluster=%ld line=%ld\n",
                 cpu.model, cpu.cores, cpu.perf_cores, cpu.l1d, cpu.l2_per_cluster, cpu.line);
    exp::request_performance_core();

    if (cfg.stage == "verify") return run_verify(cfg);

    World w = make_world(cfg.seed);
    exp::LatencyRecorder rec(size_t(kEvents - kWarmup));
    const uint64_t wall0 = exp::now_ticks();
    Result r;
    const char* extra = "";

    if (cfg.stage == "0") {
        r = run_baseline(w, rec);
    } else if (cfg.stage == "1") {
        r = run_batch(w, rec, cfg.batch);
    } else if (cfg.stage == "2") {
        r = run_prefetch(w, rec, cfg.batch, cfg.hint, cfg.distance);
    } else if (cfg.stage == "3") {
        r = run_evict(w, rec, cfg.batch, cfg.hint, cfg.distance);
    } else if (cfg.stage == "4a") {
        cfg.batch = kDualBatch;
        r = run_dual_core(w, rec, DualMode::FullResults, cfg.split);
        extra = dual_name(DualMode::FullResults);
    } else if (cfg.stage == "4b") {
        cfg.batch = kDualBatch;
        r = run_dual_core(w, rec, DualMode::Compact, cfg.split);
        extra = dual_name(DualMode::Compact);
    } else if (cfg.stage == "4c") {
        cfg.batch = kDualBatch;
        r = run_dual_core(w, rec, DualMode::CompactPublish, cfg.split);
        extra = dual_name(DualMode::CompactPublish);
    } else {
        usage(argv[0]);
        return 2;
    }

    const uint64_t wall_ns = exp::ticks_to_ns(exp::now_ticks() - wall0);
    const exp::LatencyRecorder::Summary s = rec.summarize();
    print_result(cfg, s, r,
                 extra[0] ? extra : "");
    std::printf("  wall=%.2f ms  checksum: hits=%llu qsum=%lld mix=0x%llx\n",
                double(wall_ns) / 1e6, (unsigned long long)r.hits, (long long)r.qsum,
                (unsigned long long)r.mix);
    print_csv(cfg, s, r);
    return 0;
}
