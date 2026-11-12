// example-pmr.cpp — C++17 pmr：多态内存资源、资源传播与嵌套容器
// 编译: clang++ -std=c++20 -O2 -Wall example-pmr.cpp -o example-pmr && ./example-pmr
// 说明: pmr 是 C++17 特性，用 C++20 编译只是为了和本模块其他 example 保持一致。

#include <array>
#include <cstddef>
#include <cstdio>
#include <memory_resource>
#include <new>
#include <string>
#include <vector>

int main() {
    std::printf("=== 1. pmr 容器的额外开销 ===\n");
    std::printf("sizeof(std::vector<int>)=%zu  sizeof(std::pmr::vector<int>)=%zu\n",
                sizeof(std::vector<int>), sizeof(std::pmr::vector<int>));
    std::printf("sizeof(std::string)=%zu      sizeof(std::pmr::string)=%zu\n",
                sizeof(std::string), sizeof(std::pmr::string));

    std::array<std::byte, 4096> buffer{};
    std::pmr::monotonic_buffer_resource resource{buffer.data(), buffer.size()};
    auto offset_of = [&](const void* p) {
        return static_cast<const std::byte*>(p) - buffer.data();
    };
    auto in_buffer = [&](const void* p, std::size_t bytes) {
        const auto* b = static_cast<const std::byte*>(p);
        return b >= buffer.data() && b + bytes <= buffer.data() + buffer.size();
    };

    std::printf("\n=== 2. monotonic_buffer_resource 从栈缓冲区分配 ===\n");
    std::pmr::vector<int> v{&resource};
    v.push_back(1);
    v.push_back(2);
    std::printf("v.data() 在缓冲区内=%d off=%td\n", static_cast<int>(in_buffer(v.data(), v.size() * sizeof(int))),
                offset_of(v.data()));

    std::printf("\n=== 3. 拷贝构造不继承源资源 ===\n");
    std::pmr::vector<int> copy = v;   // 走 select_on_container_copy_construction
    std::printf("copy 资源==&resource=%d  copy 资源==默认资源=%d\n",
                static_cast<int>(copy.get_allocator().resource() == &resource),
                static_cast<int>(copy.get_allocator().resource() == std::pmr::get_default_resource()));
    std::printf("copy.data() 在缓冲区内=%d（拷贝走默认堆）\n",
                static_cast<int>(in_buffer(copy.data(), copy.size() * sizeof(int))));

    std::printf("\n=== 4. 跨资源移动赋值：逐元素移动，不接管缓冲区 ===\n");
    std::array<std::byte, 4096> buffer2{};
    std::pmr::monotonic_buffer_resource resource2{buffer2.data(), buffer2.size()};
    std::pmr::vector<int> a{&resource};
    a.push_back(7);
    std::pmr::vector<int> b{&resource2};
    b.push_back(9);
    const int* b_buffer = b.data();
    a = std::move(b);   // propagate_on_move_assignment 为 false，且资源不相等
    std::printf("a 接管了 b 的缓冲区=%d（0 = 逐元素移动）  a[0]=%d  a 资源仍是 resource=%d\n",
                static_cast<int>(a.data() == b_buffer), a[0],
                static_cast<int>(a.get_allocator().resource() == &resource));

    std::printf("\n=== 5. 嵌套 pmr 容器：uses_allocator 自动传播 ===\n");
    std::pmr::vector<std::pmr::string> nested{&resource};
    nested.emplace_back(200, 'x');   // 超过 SSO，必须分配
    std::printf("nested[0] 数据在缓冲区内=%d off=%td\n",
                static_cast<int>(in_buffer(nested[0].data(), nested[0].size() + 1)),
                offset_of(nested[0].data()));

    std::printf("\n=== 6. pool 资源复用与 null_memory_resource ===\n");
    std::pmr::unsynchronized_pool_resource pool;
    void* p1 = pool.allocate(64, 8);
    pool.deallocate(p1, 64, 8);
    void* p2 = pool.allocate(64, 8);
    std::printf("pool 释放后复用同一块=%d\n", static_cast<int>(p1 == p2));

    try {
        std::pmr::vector<int> nv{std::pmr::null_memory_resource()};
        nv.push_back(1);
        std::printf("unexpected: null_memory_resource 分配成功\n");
    } catch (const std::bad_alloc&) {
        std::printf("null_memory_resource 拋 bad_alloc=1\n");
    }
}
