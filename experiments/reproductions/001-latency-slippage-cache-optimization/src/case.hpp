#pragma once

// 复现案例的数据模型与业务计算。
//
// 场景（对应原文）：每 5 微秒收到一条标的行情，标的价格变化后需要重算
// 该标的关联的 64 个期权；alpha 信号越过阈值时把该期权加入命中列表。
//
// 布局对应关系：
//   原文 Intel：缓存行 64B；事件 3 行(192B)，股票元数据 9 行(576B)，
//   每个期权 2 行(128B)，64 个期权 = 128 行。
//   Apple M2 Pro：缓存行 128B；为了保留“事件 3 行 / 元数据 9 行 / 期权 2 行”
//   的访存形态，本实验把对象按 128B 行填充，字节数相应翻倍。
 //
// 期权在每只股票的 16KB 块内按随机顺序排列（order 表），避免硬件顺序
// 预取器把“顺序扫描”优化成免费 prefetch，从而掩盖 MLP/预取实验的差异。

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include "../../../common/bench.hpp"

namespace case001 {

inline constexpr int kOptionsPerStock = 64;
#ifndef CASE_STOCKS
#define CASE_STOCKS 16384
#endif
#ifndef CASE_EVENTS
#define CASE_EVENTS 240000
#endif
#ifndef CASE_WARMUP
#define CASE_WARMUP 40000
#endif
inline constexpr int kStocks = CASE_STOCKS;
inline constexpr int kEvents = CASE_EVENTS;
inline constexpr int kWarmup = CASE_WARMUP;
inline constexpr size_t kLine = 128;

// ---------------------------------------------------------------------------
// 数据模型
// ---------------------------------------------------------------------------

// 每个期权 2 条 M2 缓存行：
//   line A：静态合约信息（只读）
//   line B：可变状态（每次事件写）
struct alignas(kLine) Option {
    // line A
    double strike;
    double vol;
    double sqrt_maturity;
    double multiplier;
    int32_t type;       // 0 = call, 1 = put
    int32_t stock_id;
    int32_t idx;        // 逻辑序号 0..63
    int32_t pad0;
    char pad_a[kLine - 48];

    // line B
    double price;
    double delta;
    double gamma;
    double vega;
    int64_t qprice;     // price * 1e9，用于跨版本精确校验
    int32_t hit;
    int32_t pad1;
    char pad_b[kLine - 48];
};
static_assert(sizeof(Option) == 2 * kLine, "Option must occupy exactly two cache lines");
static_assert(alignof(Option) == kLine);

// 股票元数据：line 0 + 8 行 vol surface = 9 条缓存行。
struct alignas(kLine) StockMeta {
    // line 0
    double spot;
    double prev_spot;
    double rate;
    double div;
    uint8_t order[kOptionsPerStock]; // 逻辑期权 -> 块内物理槽位
    char pad0[kLine - 96];

    // line 1..8：每行 16 个 double
    double surface[8][16];
};
static_assert(sizeof(StockMeta) == 9 * kLine, "StockMeta must occupy exactly nine cache lines");
static_assert(alignof(StockMeta) == kLine);

// 行情事件：3 条缓存行。
struct alignas(kLine) Event {
    // line 0
    int64_t seq;
    int32_t stock_id;
    int32_t kind;
    double spot;
    double spread;
    int64_t threshold_q;
    double drift;
    double noise;
    char pad0[kLine - 56];

    // line 1
    double params[16];
    // line 2
    double extra[16];
};
static_assert(sizeof(Event) == 3 * kLine, "Event must occupy exactly three cache lines");
static_assert(alignof(Event) == kLine);

// ---------------------------------------------------------------------------
// 确定性随机数：保证四个版本看到完全相同的事件与期权排列。
// ---------------------------------------------------------------------------

struct Rng {
    uint64_t s;

    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9e3779b97f4a7c15ull) {}

    uint64_t next() {
        uint64_t x = s;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        s = x;
        return x;
    }

    double uniform() {
        return double(next() >> 11) * (1.0 / 9007199254740992.0);
    }
};

// ---------------------------------------------------------------------------
// 业务计算：四个版本共用，保证“数学逻辑完全不变”。
// ---------------------------------------------------------------------------

inline double signal(double d) {
    // 有界、无分支的有理式替身，用来固定每个期权的计算量；
    // 目的不是估值精度，而是让访存成为主要瓶颈且各版本计算量相同。
    return 0.5 + 0.5 * d / (1.0 + (d < 0.0 ? -d : d));
}

// 从事件行中抽出计算所需的标量；双核版本会把同样的字段打包进共享槽位，
// 避免辅助核去读主核已经读过的事件缓存行。
struct Inputs {
    double spot;
    int64_t threshold_q;
    double q0, q1, q2, q3;
    double x0, x1, x2;
};

inline Inputs inputs_from(const Event& e) {
    Inputs in{};
    in.spot = e.spot;
    in.threshold_q = e.threshold_q;
    in.q0 = e.params[0];
    in.q1 = e.params[5];
    in.q2 = e.params[11];
    in.q3 = e.params[15];
    in.x0 = e.extra[0];
    in.x1 = e.extra[7];
    in.x2 = e.extra[15];
    return in;
}

inline int64_t evaluate_option(Option& o, const StockMeta& s, const Inputs& in) {
    const double spot = in.spot;
    const double strike = o.strike;
    const double t = o.sqrt_maturity;
    const double row = double(o.idx & 7);

    // 触碰 vol surface 的 8 条缓存行与股票元数据首行。
    const double w0 = s.surface[o.idx & 7][0];
    const double w1 = s.surface[o.idx & 7][1];
    const double w2 = s.surface[o.idx & 7][8];
    const double w3 = s.surface[o.idx & 7][15];

    const double q0 = in.q0;
    const double q1 = in.q1;
    const double q2 = in.q2;
    const double q3 = in.q3;
    const double x0 = in.x0;
    const double x1 = in.x1;
    const double x2 = in.x2;

    const double vol = o.vol * (1.0 + 0.25 * (w0 - w1) + 0.01 * (w2 - w3));
    const double moneyness = (spot - strike) / strike;
    const double d = (moneyness + s.rate * t + 0.5 * vol * vol * t + 0.01 * (q0 - q1)) /
                     (vol * t + 0.08);
    const double p = signal(d);

    const double intrinsic = std::max(o.type == 0 ? spot - strike : strike - spot, 0.0);
    const double time_value = strike * vol * t * p * o.multiplier;
    const double price = intrinsic + time_value + 0.001 * (q2 - q3 + x0 - x1) + 0.0001 * x2;
    const double delta = p * o.multiplier * (o.type == 0 ? 1.0 : -1.0) + 0.01 * (w0 + row);
    const double gamma = p * (1.0 - p) / (strike * vol * t + 0.1) + 0.001 * w1;
    const double vega = strike * t * p * (1.0 - p) * 0.01 + 0.0001 * w2;
    const double carry = s.rate - s.div + 0.01 * s.prev_spot / (s.spot + 1.0);

    o.price = price;
    o.delta = delta + carry;
    o.gamma = gamma;
    o.vega = vega;

    const int64_t q = int64_t(price * 1e9);
    o.qprice = q;
    o.hit = q > in.threshold_q ? 1 : 0;
    return q;
}

// ---------------------------------------------------------------------------
// 世界
// ---------------------------------------------------------------------------

struct World {
    std::vector<StockMeta> stocks;
    std::vector<Option> pool;
    std::vector<Event> events;
};

inline Option* stock_options(World& w, int stock_id) {
    return w.pool.data() + size_t(stock_id) * kOptionsPerStock;
}

inline const Option* stock_options(const World& w, int stock_id) {
    return w.pool.data() + size_t(stock_id) * kOptionsPerStock;
}

inline World make_world(uint64_t seed = 20261105ull) {
    World w;
    w.stocks.resize(kStocks);
    w.pool.resize(size_t(kStocks) * kOptionsPerStock);
    w.events.resize(kEvents);

    // 检查 over-aligned vector 的实际对齐，避免 layout 假设悄悄失效。
    if (reinterpret_cast<uintptr_t>(w.stocks.data()) % kLine != 0 ||
        reinterpret_cast<uintptr_t>(w.pool.data()) % kLine != 0 ||
        reinterpret_cast<uintptr_t>(w.events.data()) % kLine != 0) {
        std::fprintf(stderr, "fatal: vector allocation is not %zu-byte aligned\n", kLine);
        std::abort();
    }

    Rng rng(seed);
    for (int si = 0; si < kStocks; ++si) {
        StockMeta& s = w.stocks[size_t(si)];
        s.spot = 40.0 + rng.uniform() * 80.0;
        s.prev_spot = s.spot * (0.99 + 0.02 * rng.uniform());
        s.rate = 0.01 + 0.04 * rng.uniform();
        s.div = 0.005 * rng.uniform();
        for (int j = 0; j < kOptionsPerStock; ++j) s.order[j] = uint8_t(j);
        for (int j = kOptionsPerStock - 1; j > 0; --j) {
            int k = int(rng.next() % uint64_t(j + 1));
            std::swap(s.order[j], s.order[k]);
        }
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 16; ++col) {
                s.surface[row][col] = 0.1 + 0.3 * rng.uniform();
            }
        }

        Option* base = w.pool.data() + size_t(si) * kOptionsPerStock;
        for (int j = 0; j < kOptionsPerStock; ++j) {
            Option& o = base[s.order[j]];
            o.strike = s.spot * (0.85 + 0.30 * double(j) / double(kOptionsPerStock - 1));
            o.vol = 0.15 + 0.20 * rng.uniform();
            o.sqrt_maturity = 0.05 + 0.5 * rng.uniform();
            o.multiplier = (j % 4 == 0) ? 100.0 : 1.0;
            o.type = j & 1;
            o.stock_id = si;
            o.idx = j;
            o.price = o.strike;
            o.delta = 0.0;
            o.gamma = 0.0;
            o.vega = 0.0;
            o.qprice = 0;
            o.hit = 0;
        }
    }

    for (int i = 0; i < kEvents; ++i) {
        Event& e = w.events[size_t(i)];
        e.seq = i;
        e.stock_id = int(rng.next() % uint64_t(kStocks));
        e.kind = int(rng.next() & 1);
        const StockMeta& s = w.stocks[size_t(e.stock_id)];
        e.spot = s.spot * (0.98 + 0.04 * rng.uniform());
        e.spread = 0.0005 + 0.002 * rng.uniform();
        e.drift = (rng.uniform() - 0.5) * 0.02;
        e.noise = rng.uniform();
        e.threshold_q = int64_t((0.08 + 0.06 * rng.uniform()) * e.spot * 1e9);
        for (int k = 0; k < 16; ++k) {
            e.params[k] = rng.uniform() - 0.5;
            e.extra[k] = rng.uniform() - 0.5;
        }
    }

    // 模拟 NIC/DMA 写入的冷环形缓冲区：把所有行写回并失效，
    // 避免“生成事件”这一步骤提前把事件行留在缓存里。
    exp::evict_range(w.events.data(), w.events.size() * sizeof(Event), kLine);
    exp::evict_range(w.stocks.data(), w.stocks.size() * sizeof(StockMeta), kLine);
    exp::evict_range(w.pool.data(), w.pool.size() * sizeof(Option), kLine);
    return w;
}

struct Result {
    uint64_t hits = 0;
    int64_t qsum = 0;
    uint64_t mix = 0;

    inline void add(int idx, int64_t q) {
        hits += 1;
        qsum += q;
        mix ^= uint64_t(q) + uint64_t(idx) * 0x9e3779b97f4a7c15ull;
    }

    inline void merge(const Result& other) {
        hits += other.hits;
        qsum += other.qsum;
        mix ^= other.mix;
    }
};

inline void accumulate(Result& r, const Option& o) {
    if (o.hit) r.add(o.idx, o.qprice);
}

} // namespace case001
