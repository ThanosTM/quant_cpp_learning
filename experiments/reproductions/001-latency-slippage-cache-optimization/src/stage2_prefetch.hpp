#pragma once

#include "case.hpp"

namespace case001 {

enum class PrefetchHint { T0, Stream };

inline void prefetch_option(const Option& o, PrefetchHint hint) {
    if (hint == PrefetchHint::T0) {
        exp::prefetch_t0(&o);
        exp::prefetch_t0(reinterpret_cast<const char*>(&o) + kLine);
    } else {
        exp::prefetch_stream(&o);
        exp::prefetch_stream(reinterpret_cast<const char*>(&o) + kLine);
    }
}

// 第 2 步：软件预取。与第 1 步不同，预取指令没有目标寄存器，不占 ROB，
// 发射后即可退出；真正需要等待的只有第二趟计算。
inline Result run_prefetch(World& w, exp::LatencyRecorder& rec, int D, PrefetchHint hint,
                           int distance = 1) {
    Result total;
    for (int i = 0; i < kEvents; ++i) {
        Event& e = w.events[size_t(i)];
        const Inputs in = inputs_from(e);
        const StockMeta& s = w.stocks[size_t(e.stock_id)];
        Option* base = stock_options(w, e.stock_id);

        const bool record = i >= kWarmup;
        const uint64_t t0 = record ? exp::now_ticks() : 0;

        // 提前若干个批次发出预取；1 表示“算当前批，预取下一批”。
        for (int p = 0; p <= distance; ++p) {
            const int j = p * D;
            if (j >= kOptionsPerStock) break;
            for (int k = 0; k < D; ++k) prefetch_option(base[s.order[j + k]], hint);
        }

        for (int j = 0; j < kOptionsPerStock; j += D) {
            const int next = j + (distance + 1) * D;
            if (next < kOptionsPerStock) {
                for (int k = 0; k < D; ++k) prefetch_option(base[s.order[next + k]], hint);
            }
            for (int k = 0; k < D; ++k) {
                Option& o = base[s.order[j + k]];
                evaluate_option(o, s, in);
                accumulate(total, o);
            }
        }
        if (record) rec.record_ns(exp::ticks_to_ns(exp::now_ticks() - t0));
    }
    return total;
}

} // namespace case001
