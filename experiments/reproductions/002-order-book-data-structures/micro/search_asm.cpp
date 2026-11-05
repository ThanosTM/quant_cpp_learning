// 仅用于检查二分查找的代码生成；不参与基准。
// 编译: clang++ -std=c++20 -O2 -S micro/search_asm.cpp -o build/search_asm.s
#include <cstddef>
#include <cstdint>

struct Level {
    int32_t price;
    uint32_t volume;
};

extern "C" __attribute__((noinline)) size_t search_branchy(const Level* v, size_t n, int32_t p) {
    size_t first = 0;
    size_t len = n;
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

extern "C" __attribute__((noinline)) size_t search_branchless(const Level* v, size_t n, int32_t p) {
    size_t first = 0;
    size_t len = n;
    while (len > 0) {
        const size_t half = len / 2;
        first += size_t(v[first + half].price < p) * (len - half);
        len = half;
    }
    return first;
}
