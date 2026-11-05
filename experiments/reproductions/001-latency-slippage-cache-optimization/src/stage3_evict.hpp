#pragma once

#include "case.hpp"
#include "stage2_prefetch.hpp"

namespace case001 {

// 第 3 步：主动驱逐“死数据”。事件行在当前 tick 算完后不会再被访问，
// 但在默认替换策略下仍会占着 L1/L2。这里在事件末尾对其 3 条缓存行执行
// dc civac（x86 对应 clflushopt），把位置让给长期留存的期权元数据。
inline Result run_evict(World& w, exp::LatencyRecorder& rec, int D, PrefetchHint hint,
                        int distance = 1) {
    Result total;
    for (int i = 0; i < kEvents; ++i) {
        Event& e = w.events[size_t(i)];
        const Inputs in = inputs_from(e);
        const StockMeta& s = w.stocks[size_t(e.stock_id)];
        Option* base = stock_options(w, e.stock_id);

        const bool record = i >= kWarmup;
        const uint64_t t0 = record ? exp::now_ticks() : 0;

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

        // 事件内容已死：把 3 条事件行清扫出私有缓存。
        exp::evict_line(&e);
        exp::evict_line(reinterpret_cast<const char*>(&e) + kLine);
        exp::evict_line(reinterpret_cast<const char*>(&e) + 2 * kLine);
        exp::publish_fence();

        if (record) rec.record_ns(exp::ticks_to_ns(exp::now_ticks() - t0));
    }
    return total;
}

} // namespace case001
