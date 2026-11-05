#pragma once

#include <atomic>
#include <thread>

#include "case.hpp"
#include "stage2_prefetch.hpp"

namespace case001 {

enum class DualMode { FullResults, Compact, CompactPublish };

// 初版回传的“完整结果”：每个期权 2 条缓存行，和期权对象本身一样大。
struct alignas(kLine) FullResult {
    // line 0
    double price;
    double delta;
    int64_t qprice;
    int32_t idx;
    int32_t hit;
    char pad0[kLine - 32];

    // line 1
    double gamma;
    double vega;
    int64_t aux;
    char pad1[kLine - 24];
};
static_assert(sizeof(FullResult) == 2 * kLine);

// 解耦后只回传的命中项：16B/条，最多 64 条。
struct Hit {
    int64_t qprice;
    int32_t idx;
    int32_t pad;
};
static_assert(sizeof(Hit) == 16);

inline constexpr int kDualBatch = 8;

inline void prefetch_range(Option* base, const StockMeta& s, int end, int j, PrefetchHint hint) {
    for (int k = 0; k < kDualBatch; ++k) {
        const int idx = j + k;
        if (idx >= end) break;
        prefetch_option(base[s.order[idx]], hint);
    }
}

// 主核在发信号前把辅核的前两批期权拉到共享 L2：辅核醒来时数据已在途，
// 避免“醒来才开始追冷缺失”的数百纳秒等待。这是 M2 上的必要适配，
// 原文的双核版本没有这一步。
inline void prefetch_worker_lead(Option* base, const StockMeta& s, int first, int end) {
    for (int p = 0; p < 2; ++p) {
        const int j = first + p * kDualBatch;
        for (int k = 0; k < kDualBatch; ++k) {
            const int idx = j + k;
            if (idx >= end) break;
            const Option& o = base[s.order[idx]];
            exp::prefetch_l2(&o);
            exp::prefetch_l2(reinterpret_cast<const char*>(&o) + kLine);
        }
    }
}

// 共享槽位按缓存行分区：
//   line 0：主核 -> 辅核 的参数 + seq（release 发布）
//   line 1：辅核 -> 主核 的 done / hit_count（release 发布）
//   之后：完整结果数组（4a）与紧凑命中列表（4b/4c）
struct alignas(kLine) Shared {
    // ---- 主核写，辅核读 ----
    std::atomic<uint64_t> seq{0};
    int32_t stock_id{0};
    int32_t first{48};
    int32_t count{16};
    int32_t pad0{0};
    double spot{0.0};
    int64_t threshold_q{0};
    double q0{0.0}, q1{0.0}, q2{0.0}, q3{0.0};
    double x0{0.0}, x1{0.0}, x2{0.0};
    char pad1[kLine - 96];

    // ---- 辅核写，主核读 ----
    alignas(kLine) std::atomic<uint64_t> done{0};
    std::atomic<bool> stop{false};
    std::atomic<bool> ready{false};
    int32_t hit_count{0};
    int32_t pad2{0};
    char pad3[kLine - 24];

    alignas(kLine) FullResult full[64];
    alignas(kLine) Hit hits[64];
};

class DualCoreRunner {
public:
    DualCoreRunner(World& w, DualMode mode, int main_count = 48)
        : w_(w), mode_(mode), main_count_(main_count), worker_count_(64 - main_count) {
        shared_.first = main_count_;
        shared_.count = worker_count_;
        worker_ = std::thread([this] { worker_loop(); });
        while (!shared_.ready.load(std::memory_order_acquire)) exp::cpu_relax();
    }

    ~DualCoreRunner() {
        shared_.stop.store(true, std::memory_order_release);
        if (worker_.joinable()) worker_.join();
    }

    Result run_one(const Event& e, bool record, exp::LatencyRecorder& rec) {
        const Inputs in = inputs_from(e);
        const StockMeta& s = w_.stocks[size_t(e.stock_id)];
        Option* base = stock_options(w_, e.stock_id);
        const uint64_t ticket = uint64_t(e.seq) + 1;

        const uint64_t t0 = record ? exp::now_ticks() : 0;

        // 打包参数：辅核不再去读主核已经读过的事件行。
        shared_.stock_id = e.stock_id;
        shared_.spot = in.spot;
        shared_.threshold_q = in.threshold_q;
        shared_.q0 = in.q0;
        shared_.q1 = in.q1;
        shared_.q2 = in.q2;
        shared_.q3 = in.q3;
        shared_.x0 = in.x0;
        shared_.x1 = in.x1;
        shared_.x2 = in.x2;
        prefetch_worker_lead(base, s, shared_.first, 64);
        shared_.seq.store(ticket, std::memory_order_release);

        // 主核负责前 main_count_ 个期权，保留两批预取流水线。
        Result r;
        for (int p = 0; p < 2; ++p) {
            prefetch_range(base, s, main_count_, p * kDualBatch, PrefetchHint::T0);
        }
        for (int j = 0; j < main_count_; j += kDualBatch) {
            const int next = j + 2 * kDualBatch;
            if (next < main_count_) prefetch_range(base, s, main_count_, next, PrefetchHint::T0);
            for (int k = 0; k < kDualBatch && j + k < main_count_; ++k) {
                Option& o = base[s.order[j + k]];
                evaluate_option(o, s, in);
                accumulate(r, o);
            }
        }

        // 等待辅核完成剩余期权。
        while (shared_.done.load(std::memory_order_acquire) != ticket) exp::cpu_relax();

        if (mode_ == DualMode::FullResults) {
            // 初版：把辅核的完整中间结果全部读回来（每个 2 行）。
            for (int k = 0; k < worker_count_; ++k) {
                const FullResult& fr = shared_.full[k];
                exp::touch_line(reinterpret_cast<const char*>(&fr) + kLine);
                exp::consume_u64(uint64_t(fr.aux));
                if (fr.hit) r.add(fr.idx, fr.qprice);
            }
        } else {
            // 解耦：只读命中列表，不碰辅核的期权状态行。
            const int hc = shared_.hit_count;
            for (int k = 0; k < hc; ++k) {
                const Hit& h = shared_.hits[k];
                r.add(h.idx, h.qprice);
            }
        }

        if (record) rec.record_ns(exp::ticks_to_ns(exp::now_ticks() - t0));
        return r;
    }

private:
    void worker_loop() {
        exp::request_performance_core();
        shared_.ready.store(true, std::memory_order_release);
        uint64_t last = 0;
        while (!shared_.stop.load(std::memory_order_relaxed)) {
            const uint64_t ticket = shared_.seq.load(std::memory_order_acquire);
            if (ticket == last) {
                exp::cpu_relax();
                continue;
            }
            last = ticket;

            const int32_t stock_id = shared_.stock_id;
            const StockMeta& s = w_.stocks[size_t(stock_id)];
            Option* base = stock_options(w_, stock_id);

            Inputs in{};
            in.spot = shared_.spot;
            in.threshold_q = shared_.threshold_q;
            in.q0 = shared_.q0;
            in.q1 = shared_.q1;
            in.q2 = shared_.q2;
            in.q3 = shared_.q3;
            in.x0 = shared_.x0;
            in.x1 = shared_.x1;
            in.x2 = shared_.x2;

            // 主核已把前两批拉到共享 L2；辅核只对前两批发 L1 预取，
            // 其余批次在下面的循环里提前一批预取，避免一次打满 Fill Buffer。
            for (int p = 0; p < 2; ++p) {
                const int j = shared_.first + p * kDualBatch;
                if (j >= 64) break;
                prefetch_range(base, s, 64, j, PrefetchHint::T0);
            }

            int hc = 0;
            for (int j = shared_.first; j < 64; j += kDualBatch) {
                const int next = j + 2 * kDualBatch;
                if (next < 64) prefetch_range(base, s, 64, next, PrefetchHint::T0);
                for (int k = 0; k < kDualBatch && j + k < 64; ++k) {
                    Option& o = base[s.order[j + k]];
                    const int64_t q = evaluate_option(o, s, in);
                    if (mode_ == DualMode::FullResults) {
                        FullResult& fr = shared_.full[j + k - shared_.first];
                        fr.price = o.price;
                        fr.delta = o.delta;
                        fr.qprice = q;
                        fr.idx = o.idx;
                        fr.hit = o.hit;
                        fr.gamma = o.gamma;
                        fr.vega = o.vega;
                        fr.aux = q ^ int64_t(o.idx);
                    } else if (o.hit) {
                        Hit& h = shared_.hits[hc++];
                        h.qprice = q;
                        h.idx = o.idx;
                    }
                }
            }

            if (mode_ == DualMode::CompactPublish && hc > 0) {
                char* p = reinterpret_cast<char*>(shared_.hits);
                const size_t bytes = size_t(hc) * sizeof(Hit);
                for (size_t off = 0; off < bytes; off += kLine) exp::publish_line(p + off);
                exp::publish_fence();
            }

            shared_.hit_count = hc;
            shared_.done.store(ticket, std::memory_order_release);
        }
    }

    World& w_;
    DualMode mode_;
    int main_count_;
    int worker_count_;
    Shared shared_;
    std::thread worker_;
};

inline Result run_dual_core(World& w, exp::LatencyRecorder& rec, DualMode mode,
                            int main_count = 48) {
    exp::request_performance_core();
    DualCoreRunner runner(w, mode, main_count);
    Result total;
    for (int i = 0; i < kEvents; ++i) {
        Result r = runner.run_one(w.events[size_t(i)], i >= kWarmup, rec);
        total.merge(r);
    }
    return total;
}

} // namespace case001
