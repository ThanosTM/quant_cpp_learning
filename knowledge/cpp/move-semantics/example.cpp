// example.cpp —— 移动语义三连演示：移动触发、noexcept 与 vector 扩容、moved-from 状态
// 编译: g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
//       (macOS 可用 clang++ 替换 g++)
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <utility>
#include <vector>

// ---------- 1. Rule of Five 示范类：移动构造带 noexcept ----------
class Buffer {
public:
    explicit Buffer(std::size_t n) : size_(n), data_(new int[n]{}) {}

    Buffer(const Buffer& o) : size_(o.size_), data_(new int[o.size_]) {
        std::copy(o.data_, o.data_ + size_, data_);
        ++copy_count_;
    }

    Buffer(Buffer&& o) noexcept  // noexcept 是 vector 扩容用移动的前提
        : size_(o.size_), data_(o.data_) {
        o.size_ = 0;
        o.data_ = nullptr;  // 关键：置空源对象，避免双重释放
        ++move_count_;
    }

    Buffer& operator=(const Buffer& o) {
        if (this != &o) {
            delete[] data_;
            data_ = new int[o.size_];
            size_ = o.size_;
            std::copy(o.data_, o.data_ + size_, data_);
        }
        return *this;
    }

    Buffer& operator=(Buffer&& o) noexcept {
        if (this != &o) {
            delete[] data_;
            data_ = o.data_;
            size_ = o.size_;
            o.data_ = nullptr;
            o.size_ = 0;
        }
        return *this;
    }

    ~Buffer() { delete[] data_; }

    static int copy_count_;  // 观测拷贝/移动发生次数
    static int move_count_;

private:
    std::size_t size_;
    int* data_;
};
int Buffer::copy_count_ = 0;
int Buffer::move_count_ = 0;

// ---------- 2. 对照类：唯一区别是移动构造缺少 noexcept ----------
class BadBuffer {
public:
    explicit BadBuffer(std::size_t n) : size_(n), data_(new int[n]{}) {}

    BadBuffer(const BadBuffer& o) : size_(o.size_), data_(new int[o.size_]) {
        std::copy(o.data_, o.data_ + size_, data_);
        ++copy_count_;
    }

    BadBuffer(BadBuffer&& o)  // 注意：没有 noexcept
        : size_(o.size_), data_(o.data_) {
        o.size_ = 0;
        o.data_ = nullptr;
        ++move_count_;
    }

    ~BadBuffer() { delete[] data_; }

    static int copy_count_;
    static int move_count_;

private:
    std::size_t size_;
    int* data_;
};
int BadBuffer::copy_count_ = 0;
int BadBuffer::move_count_ = 0;

int main() {
    std::cout << "== 实验 1: push_back 右值触发移动构造 ==\n";
    {
        Buffer b1{1000};
        std::vector<Buffer> v;
        v.reserve(1);
        v.push_back(std::move(b1));  // b1 被 cast 成右值 -> 选移动构造
        std::cout << "  copy=" << Buffer::copy_count_ << " move=" << Buffer::move_count_
                  << "  (期望 copy=0 move=1)\n";
    }

    std::cout << "== 实验 2: vector 扩容时 noexcept 移动 vs 拷贝 ==\n";
    {
        std::vector<Buffer> v;
        for (int i = 0; i < 8; ++i) v.push_back(Buffer{16});  // 容量 1->2->4->8
        std::cout << "  Buffer (noexcept): copy=" << Buffer::copy_count_
                  << " move=" << Buffer::move_count_ << "  (期望全程 0 拷贝)\n";
    }
    {
        std::vector<BadBuffer> v;
        for (int i = 0; i < 8; ++i) v.push_back(BadBuffer{16});
        std::cout << "  BadBuffer (非 noexcept): copy=" << BadBuffer::copy_count_
                  << " move=" << BadBuffer::move_count_
                  << "  (扩容搬运退化为拷贝: copy=1+2+4=7)\n";
    }

    std::cout << "== 实验 3: moved-from 状态可安全重新赋值 ==\n";
    Buffer b2{5};
    Buffer b3 = std::move(b2);
    (void)b3;
    b2 = Buffer{7};  // 合法：moved-from 对象处于"有效但未指定"状态
    std::cout << "  b2 重新赋值 OK，析构安全\n";
    return 0;
}
