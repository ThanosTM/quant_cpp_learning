// example.cpp — 分配器、pmr 内存资源、对象池与 placement new
// 编译: clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <memory_resource>
#include <new>
#include <string>
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

struct Order {
    int id = 0;
    double price = 0.0;
    char pad[16]{};
};

// 固定容量对象池：内存来自对象内部，运行期零 malloc
class OrderPool {
public:
    static constexpr std::size_t kCapacity = 4;

    Order* allocate(int id, double price) {
        for (std::size_t i = 0; i < kCapacity; ++i) {
            if (!used_[i]) {
                used_[i] = true;
                ++g_pool_allocations;
                return new (storage_ + i * sizeof(Order)) Order{id, price, {}};
            }
        }
        return nullptr;
    }

    void deallocate(Order* p) {
        if (!p) return;
        p->~Order();
        const std::size_t index = (reinterpret_cast<std::byte*>(p) - storage_) / sizeof(Order);
        used_[index] = false;
    }

    static std::size_t pool_allocations() { return g_pool_allocations; }

private:
    alignas(Order) std::byte storage_[kCapacity * sizeof(Order)]{};
    bool used_[kCapacity]{};
    static inline std::size_t g_pool_allocations = 0;
};

struct alignas(64) CacheLineAligned {
    int value = 0;
};

}  // namespace

int main() {
    std::printf("=== 1. 自定义分配器统计 ===\n");
    {
        g_allocations = 0;
        std::vector<int, CountingAllocator<int>> v;
        for (int i = 0; i < 100; ++i) v.push_back(i);
        std::printf("push_back(100): allocations=%zu capacity=%zu\n", g_allocations, v.capacity());

        g_allocations = 0;
        std::vector<int, CountingAllocator<int>> w;
        w.reserve(100);
        for (int i = 0; i < 100; ++i) w.push_back(i);
        std::printf("reserve(100):  allocations=%zu capacity=%zu\n", g_allocations, w.capacity());
    }

    std::printf("\n=== 2. pmr::monotonic_buffer_resource ===\n");
    {
        std::array<std::byte, 4096> buffer{};
        std::pmr::monotonic_buffer_resource resource{buffer.data(), buffer.size()};

        std::pmr::vector<int> v{&resource};
        v.reserve(100);
        for (int i = 0; i < 100; ++i) v.push_back(i);

        std::pmr::string s(200, 'm', &resource);   // 超过 SSO，必然走资源分配

        const auto* begin = buffer.data();
        const auto* end = buffer.data() + buffer.size();
        auto in_buffer = [&](const void* p) {
            const auto* b = static_cast<const std::byte*>(p);
            return b >= begin && b < end;
        };
        auto offset_of = [&](const void* p) {
            return static_cast<const std::byte*>(p) - begin;
        };

        std::printf("vector 在缓冲区内=%d (off=%td)，string 在内部=%d (off=%td)，缓冲区=%zu 字节\n",
                    static_cast<int>(in_buffer(v.data())), offset_of(v.data()),
                    static_cast<int>(in_buffer(s.data())), offset_of(s.data()), buffer.size());
        std::printf("本机 libc++ 从缓冲区尾部向前分配（实现相关，不要依赖方向）\n");
    }

    std::printf("\n=== 3. 固定容量对象池 ===\n");
    {
        OrderPool pool;
        Order* a = pool.allocate(1, 10.5);
        Order* b = pool.allocate(2, 20.5);
        std::printf("a=%p b=%p 间距=%td 池内分配次数=%zu\n", static_cast<void*>(a),
                    static_cast<void*>(b),
                    reinterpret_cast<std::byte*>(b) - reinterpret_cast<std::byte*>(a),
                    OrderPool::pool_allocations());
        pool.deallocate(a);
        Order* c = pool.allocate(3, 30.5);
        std::printf("释放后复用同一槽位=%d id=%d\n", static_cast<int>(c == a), c->id);
        pool.deallocate(b);
        pool.deallocate(c);
    }

    std::printf("\n=== 4. placement new 与对齐 ===\n");
    {
        alignas(64) std::byte storage[sizeof(CacheLineAligned)];
        auto* obj = new (storage) CacheLineAligned{42};
        auto addr = reinterpret_cast<std::uintptr_t>(obj);
        std::printf("placement new: value=%d 地址%%64=%zu\n", obj->value, addr % 64);

        auto heap = std::make_unique<CacheLineAligned>();
        auto heap_addr = reinterpret_cast<std::uintptr_t>(heap.get());
        std::printf("make_unique<alignas(64)>: 地址%%64=%zu\n", heap_addr % 64);

        obj->~CacheLineAligned();
    }

    std::printf("\n=== 5. 分配器选择速查 ===\n");
    std::printf("std::allocator: 通用堆分配；pmr::monotonic: 批量小对象、整块释放；\n");
    std::printf("对象池: 固定大小、稳定地址；placement new: 已有内存上构造。\n");
}
