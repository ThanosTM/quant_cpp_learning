#pragma once

#include "case.hpp"

namespace case001 {

// 第 0 步：串行逐个重算。每个期权先读静态行、再写状态行，前一个期权的
// 缺失还没回来就开始下一个，完全依赖乱序窗口自然形成的少量重叠。
inline Result run_baseline(World& w, exp::LatencyRecorder& rec) {
    Result total;
    for (int i = 0; i < kEvents; ++i) {
        Event& e = w.events[size_t(i)];
        const Inputs in = inputs_from(e);
        const StockMeta& s = w.stocks[size_t(e.stock_id)];
        Option* base = stock_options(w, e.stock_id);

        const bool record = i >= kWarmup;
        const uint64_t t0 = record ? exp::now_ticks() : 0;
        for (int j = 0; j < kOptionsPerStock; ++j) {
            Option& o = base[s.order[j]];
            evaluate_option(o, s, in);
            accumulate(total, o);
        }
        if (record) rec.record_ns(exp::ticks_to_ns(exp::now_ticks() - t0));
    }
    return total;
}

} // namespace case001
