#pragma once

// 订单簿有序结构复现：std::map → vector → 反转 vector → 无分支二分 → 线性搜索。
//
// 只复现“买盘一侧”的有序价格层级，隔离掉 orderId -> price 的哈希查找；
// 这样比较的是纯有序结构的插入/修改/删除成本，而不是哈希表性能。

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>
#include <vector>

#include "../../../common/bench.hpp"

namespace ob {

using Price = int32_t;
using Volume = uint32_t;

enum class Op : uint8_t { Add = 0, Set = 1, Delete = 2 };

struct Event {
    uint8_t op;
    Price price;
    Volume volume;
};
static_assert(sizeof(Event) == 12, "Event must be 12 bytes");

struct Level {
    Price price;
    Volume volume;
};

struct FileHeader {
    uint64_t magic;
    uint32_t version;
    uint32_t ops;
    uint32_t max_levels;
    uint32_t reserved;
};
inline constexpr uint64_t kMagic = 0x4f424b303032ull;
inline constexpr uint32_t kVersion = 1;
static_assert(sizeof(FileHeader) == 24, "FileHeader must be 24 bytes");

// ---------------------------------------------------------------------------
// 确定性随机数
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
    double uniform() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
};

// ---------------------------------------------------------------------------
// 事件生成用的参考订单簿：有序 vector + 二分，生成阶段不计时。
// 事件的价格分布是“头部主导”：绝大多数更新发生在最优价附近。
// ---------------------------------------------------------------------------

class RefBook {
public:
    RefBook(int max_levels) : max_levels_(max_levels) {
        for (int i = 0; i < 64; ++i) {
            levels_.push_back({1'000'000 + i, Volume(100 + i)});
        }
    }

    Price best() const { return levels_.empty() ? 1'000'000 : levels_.back().price; }

    void apply(const Event& e) {
        auto it = std::lower_bound(levels_.begin(), levels_.end(), e.price,
                                   [](const Level& l, Price p) { return l.price < p; });
        if (it != levels_.end() && it->price == e.price) {
            if (e.op == uint8_t(Op::Delete)) {
                levels_.erase(it);
            } else if (e.op == uint8_t(Op::Add)) {
                it->volume += e.volume;
            } else {
                it->volume = e.volume;
            }
        } else if (e.op != uint8_t(Op::Delete) && int(levels_.size()) < max_levels_) {
            // 订单簿有最大深度：满了以后新价格层级不再插入，等待删除腾出位置。
            levels_.insert(it, {e.price, e.volume});
        }
    }

    size_t size() const { return levels_.size(); }

private:
    std::vector<Level> levels_;
    int max_levels_;
};

inline Event make_event(Rng& rng, const RefBook& ref) {
    Event e{};
    const double r = rng.uniform();
    const Price best = ref.best();

    if (r < 0.30) {
        // 修改最优价：真实市场里最常见的操作
        e.op = uint8_t(Op::Set);
        e.price = best;
        e.volume = Volume(1 + rng.next() % 1000);
    } else if (r < 0.60) {
        // 在最优价上方 1~8 tick 插入/加量：新最优价，反转 vector 的 push_back 路径
        e.op = uint8_t(Op::Add);
        e.price = best + 1 + Price(rng.next() % 8);
        e.volume = Volume(1 + rng.next() % 200);
    } else if (r < 0.70) {
        // 盘口内部 1~16 tick
        e.op = uint8_t(Op::Add);
        e.price = best - 1 - Price(rng.next() % 16);
        e.volume = Volume(1 + rng.next() % 100);
    } else if (r < 0.75) {
        // 更深的层级，插入时会造成 vector 搬移
        e.op = uint8_t(Op::Set);
        e.price = best - 17 - Price(rng.next() % 240);
        e.volume = Volume(1 + rng.next() % 100);
    } else {
        // 删掉最优价附近：让 best 向下移动；反转 vector 的 pop_back 路径
        e.op = uint8_t(Op::Delete);
        e.price = best - Price(rng.next() % 8);
        e.volume = 0;
    }
    return e;
}

inline bool write_events(const char* path, uint32_t ops, uint32_t max_levels) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    FileHeader h{kMagic, kVersion, ops, max_levels, 0};
    if (std::fwrite(&h, sizeof(h), 1, f) != 1) {
        std::fclose(f);
        return false;
    }

    Rng rng(20261105ull);
    RefBook ref{int(max_levels)};
    std::vector<Event> buf;
    buf.reserve(4096);

    for (uint32_t i = 0; i < ops; ++i) {
        Event e = make_event(rng, ref);
        ref.apply(e);
        buf.push_back(e);
        if (buf.size() == 4096) {
            std::fwrite(buf.data(), sizeof(Event), buf.size(), f);
            buf.clear();
        }
    }
    if (!buf.empty()) std::fwrite(buf.data(), sizeof(Event), buf.size(), f);
    std::fclose(f);
    return true;
}

inline bool load_events(const char* path, std::vector<Event>& events, uint32_t& max_levels) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    FileHeader h{};
    if (std::fread(&h, sizeof(h), 1, f) != 1 || h.magic != kMagic || h.version != kVersion) {
        std::fclose(f);
        return false;
    }
    events.resize(h.ops);
    const size_t got = std::fread(events.data(), sizeof(Event), events.size(), f);
    std::fclose(f);
    if (got != events.size()) return false;
    max_levels = h.max_levels;
    return true;
}

// ---------------------------------------------------------------------------
// 校验和
// ---------------------------------------------------------------------------

struct Check {
    uint64_t count = 0;
    int64_t sum = 0;
    uint64_t mix = 0;

    inline void add(Price p, Volume v) {
        count += 1;
        sum += int64_t(p) * int64_t(v);
        mix ^= (uint64_t(p) * 0x9e3779b97f4a7c15ull) + uint64_t(v);
    }

    bool operator==(const Check& o) const {
        return count == o.count && sum == o.sum && mix == o.mix;
    }
};

// ---------------------------------------------------------------------------
// 1) std::map：节点容器
// ---------------------------------------------------------------------------

class MapBook {
public:
    explicit MapBook(int max_levels) : max_levels_(max_levels) {}

    void apply(const Event& e) {
        auto it = levels_.lower_bound(e.price);
        if (it != levels_.end() && it->first == e.price) {
            if (e.op == uint8_t(Op::Delete)) {
                levels_.erase(it);
            } else if (e.op == uint8_t(Op::Add)) {
                it->second += e.volume;
            } else {
                it->second = e.volume;
            }
        } else if (e.op != uint8_t(Op::Delete) && int(levels_.size()) < max_levels_) {
            levels_.emplace(e.price, e.volume);
        }
    }

    Check check() const {
        Check c;
        for (const auto& kv : levels_) c.add(kv.first, kv.second);
        return c;
    }

private:
    std::map<Price, Volume> levels_;
    int max_levels_;
};

// ---------------------------------------------------------------------------
// 2) vector：两种布局与四种查找
// ---------------------------------------------------------------------------

enum class Layout { Ascending, Descending };
enum class Search { Std, Branchy, Branchless, Linear };

inline size_t lower_bound_branchy(const std::vector<Level>& v, Price p) {
    size_t first = 0;
    size_t len = v.size();
    while (len > 0) {
        const size_t half = len / 2;
        if (v[first + half].price < p) {
            first += len - half;
            len = half;
        } else {
            len = half;
        }
    }
    return first;
}

inline size_t lower_bound_branchless(const std::vector<Level>& v, Price p) {
    size_t first = 0;
    size_t len = v.size();
    while (len > 0) {
        const size_t half = len / 2;
        // 关键：用 0/1 乘法代替条件跳转，目标是 csel/csinc 类指令。
        first += size_t(v[first + half].price < p) * (len - half);
        len = half;
    }
    return first;
}

inline size_t linear_ascending(const std::vector<Level>& v, Price p) {
    size_t pos = v.size();
    while (pos > 0 && v[pos - 1].price >= p) --pos;
    return pos;
}

inline size_t linear_descending(const std::vector<Level>& v, Price p) {
    size_t pos = 0;
    while (pos < v.size() && v[pos].price > p) ++pos;
    return pos;
}

template <Layout L, Search S>
class VectorBook {
public:
    explicit VectorBook(int max_levels) : max_levels_(max_levels) { v_.reserve(size_t(max_levels)); }

    void apply(const Event& e) {
        const size_t pos = find(e.price);
        if (pos < v_.size() && v_[pos].price == e.price) {
            if (e.op == uint8_t(Op::Delete)) {
                v_.erase(v_.begin() + ptrdiff_t(pos));
            } else if (e.op == uint8_t(Op::Add)) {
                v_[pos].volume += e.volume;
            } else {
                v_[pos].volume = e.volume;
            }
        } else if (e.op != uint8_t(Op::Delete) && int(v_.size()) < max_levels_) {
            v_.insert(v_.begin() + ptrdiff_t(pos), {e.price, e.volume});
        }
    }

    Check check() const {
        Check c;
        for (const Level& l : v_) c.add(l.price, l.volume);
        return c;
    }

private:
    size_t find(Price p) const {
        if constexpr (S == Search::Linear) {
            return L == Layout::Ascending ? linear_ascending(v_, p) : linear_descending(v_, p);
        } else if constexpr (S == Search::Branchy) {
            return lower_bound_branchy(v_, p);
        } else if constexpr (S == Search::Branchless) {
            return lower_bound_branchless(v_, p);
        } else {
            if constexpr (L == Layout::Ascending) {
                auto it = std::lower_bound(v_.begin(), v_.end(), p,
                                           [](const Level& l, Price q) { return l.price < q; });
                return size_t(it - v_.begin());
            } else {
                auto it = std::lower_bound(v_.begin(), v_.end(), p,
                                           [](const Level& l, Price q) { return l.price > q; });
                return size_t(it - v_.begin());
            }
        }
    }

    void prune() {
        if constexpr (L == Layout::Ascending) {
            v_.erase(v_.begin());
        } else {
            v_.pop_back();
        }
    }

    std::vector<Level> v_;
    int max_levels_;
};

// ---------------------------------------------------------------------------
// 堆污染：先制造大量小块空洞，再让 map 节点分配进去。
// 只在 map-polluted 用例中启用，map-clean 保持干净堆。
// ---------------------------------------------------------------------------

class HeapPolluter {
public:
    explicit HeapPolluter(size_t blocks = 200'000) {
        blocks_.reserve(blocks);
        for (size_t i = 0; i < blocks; ++i) {
            const size_t size = 16 + 16 * (rng_.next() % 8); // 16..128B
            void* p = std::malloc(size);
            if (!p) std::abort();
            blocks_.push_back(p);
        }
        // 释放一半，留下大量与 map 节点同量级的空洞。
        for (size_t i = 1; i < blocks_.size(); i += 2) {
            std::free(blocks_[i]);
            blocks_[i] = nullptr;
        }
    }

    ~HeapPolluter() {
        for (void* p : blocks_) std::free(p);
    }

    // 在计时块之间调用：不断把部分空闲块换成新块，持续制造碎片。
    void churn(size_t n = 64) {
        for (size_t k = 0; k < n; ++k) {
            const size_t i = size_t(rng_.next() % blocks_.size());
            if (blocks_[i]) {
                std::free(blocks_[i]);
                blocks_[i] = nullptr;
            } else {
                const size_t size = 16 + 16 * (rng_.next() % 8);
                blocks_[i] = std::malloc(size);
                if (!blocks_[i]) std::abort();
            }
        }
    }

private:
    std::vector<void*> blocks_;
    Rng rng_{0x48454150ull};
};

} // namespace ob
