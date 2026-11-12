// example.cpp — STL 容器底层：扩容、迭代器失效、map vs unordered_map
// 编译: clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::size_t g_allocations = 0;

template <typename T>
struct CountingAllocator {
    using value_type = T;

    CountingAllocator() = default;
    template <typename U>
    CountingAllocator(const CountingAllocator<U>&) {}

    T* allocate(std::size_t n) {
        ++g_allocations;
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t) { ::operator delete(p); }

    template <typename U>
    bool operator==(const CountingAllocator<U>&) const { return true; }
    template <typename U>
    bool operator!=(const CountingAllocator<U>&) const { return false; }
};

template <typename Map>
double lookup_ns_per_op(const Map& m, const std::vector<int>& keys) {
    using clock = std::chrono::steady_clock;
    double best = 1e18;
    for (int round = 0; round < 3; ++round) {
        volatile long long sink = 0;
        auto t0 = clock::now();
        for (int k : keys) {
            auto it = m.find(k);
            if (it != m.end()) sink += it->second;
        }
        auto t1 = clock::now();
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        best = std::min(best, ns / static_cast<double>(keys.size()));
    }
    return best;
}

}  // namespace

int main() {
    std::printf("=== 1. vector 扩容 ===\n");
    {
        std::vector<int> v;
        std::printf("size/capacity: ");
        for (int i = 0; i < 17; ++i) {
            v.push_back(i);
            std::printf("%zu/%zu ", v.size(), v.capacity());
        }
        std::printf("\n");
    }

    std::printf("\n=== 2. 扩容导致指针失效 ===\n");
    {
        std::vector<int> v;
        v.reserve(4);
        for (int i = 0; i < 4; ++i) v.push_back(i);
        const int* before = v.data();
        std::printf("capacity=%zu data=%p\n", v.capacity(), static_cast<const void*>(before));
        v.push_back(4);
        const int* after = v.data();
        std::printf("capacity=%zu data=%p moved=%s\n", v.capacity(),
                    static_cast<const void*>(after), before == after ? "no" : "yes");
    }

    std::printf("\n=== 3. 分配次数：push_back vs reserve ===\n");
    {
        g_allocations = 0;
        std::vector<int, CountingAllocator<int>> a;
        for (int i = 0; i < 1000; ++i) a.push_back(i);
        std::printf("push_back(1000): allocations=%zu capacity=%zu\n", g_allocations, a.capacity());

        g_allocations = 0;
        std::vector<int, CountingAllocator<int>> b;
        b.reserve(1000);
        for (int i = 0; i < 1000; ++i) b.push_back(i);
        std::printf("reserve(1000):   allocations=%zu capacity=%zu\n", g_allocations, b.capacity());
    }

    std::printf("\n=== 4. map vs unordered_map 查找 ===\n");
    {
        constexpr int N = 200000;
        constexpr int Q = 100000;
        std::map<int, int> ordered;
        std::unordered_map<int, int> hashed;
        hashed.reserve(N);
        for (int i = 0; i < N; ++i) {
            ordered.emplace(i, i);
            hashed.emplace(i, i);
        }
        std::vector<int> keys;
        keys.reserve(Q);
        for (int i = 0; i < Q; ++i) keys.push_back((i * 7919) % N);

        double ordered_ns = lookup_ns_per_op(ordered, keys);
        double hashed_ns = lookup_ns_per_op(hashed, keys);
        std::printf("map          : %.1f ns/lookup (node-based, cache miss)\n", ordered_ns);
        std::printf("unordered_map: %.1f ns/lookup (bucket-based)\n", hashed_ns);
        std::printf("sizeof(map::value_type)=%zu sizeof(unordered_map::value_type)=%zu\n",
                    sizeof(std::map<int, int>::value_type),
                    sizeof(std::unordered_map<int, int>::value_type));
    }

    std::printf("\n=== 5. string 的 SSO ===\n");
    {
        auto on_stack = [](const std::string& s) {
            const char* obj = reinterpret_cast<const char*>(&s);
            return s.data() >= obj && s.data() < obj + sizeof(std::string);
        };
        std::string s = "short";
        std::printf("sizeof(std::string)=%zu short: capacity=%zu sso=%s\n", sizeof(std::string),
                    s.capacity(), on_stack(s) ? "yes" : "no");
        std::string l = "a string long enough to exceed the small buffer optimization";
        std::printf("long : capacity=%zu sso=%s\n", l.capacity(), on_stack(l) ? "yes" : "no");
    }

    std::printf("\n=== 6. 迭代器失效规则 ===\n");
    {
        std::vector<int> v{1, 2, 3};
        std::size_t cap_before = v.capacity();
        v.push_back(4);
        std::printf("vector: capacity %zu -> %zu，扩容后所有迭代器/指针/引用失效\n", cap_before,
                    v.capacity());

        std::map<int, int> m{{1, 1}, {2, 2}};
        auto mit = m.find(1);
        m.insert({3, 3});
        std::printf("map: insert 不影响其他节点，key=%d 的迭代器仍有效\n", mit->first);

        std::vector<int> w{1, 2, 3, 4};
        auto wit = w.begin() + 1;
        w.erase(w.begin());  // 删除位置之后的迭代器全部失效
        std::printf("vector erase 后 size=%zu，it 之后的迭代器需重新获取\n", w.size());
        (void)wit;
    }
}
