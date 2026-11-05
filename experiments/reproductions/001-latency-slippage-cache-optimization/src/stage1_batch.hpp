#pragma once

#include "case.hpp"

namespace case001 {

// 第 1 步：两趟遍历。第一趟只“摸”一批期权的两条缓存行，让多个 L1 缺失
// 同时在途；第二趟集中计算。D 是批次大小，用来扫出 LFB（行填充缓冲区）
// 深度附近的饱和点。
inline Result run_batch(World& w, exp::LatencyRecorder& rec, int D) {
    Result total;
    for (int i = 0; i < kEvents; ++i) {
        Event& e = w.events[size_t(i)];
        const Inputs in = inputs_from(e);
        const StockMeta& s = w.stocks[size_t(e.stock_id)];
        Option* base = stock_options(w, e.stock_id);

        const bool record = i >= kWarmup;
        const uint64_t t0 = record ? exp::now_ticks() : 0;
        for (int j = 0; j < kOptionsPerStock; j += D) {
            // 第一趟：只发起加载，不做计算。
            for (int k = 0; k < D; ++k) {
                Option& o = base[s.order[j + k]];
                exp::touch_line(&o);
            }
            // 编译器屏障：防止计算被提前调度到“摸地址”之前。
            asm volatile("" ::: "memory");

            // 第二趟：数据已在途/已到达，集中计算。
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
