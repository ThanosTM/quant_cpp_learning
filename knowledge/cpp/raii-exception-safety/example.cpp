// example.cpp — RAII 与异常安全：三种保证、copy-and-swap、scope guard
// 编译: clang++ -std=c++20 -O2 -Wall example.cpp -o example && ./example

#include <cstddef>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

int g_live = 0;

struct Tracked {
    Tracked() { ++g_live; }
    Tracked(const Tracked&) { ++g_live; }
    Tracked(Tracked&&) noexcept { ++g_live; }
    ~Tracked() { --g_live; }
};

struct FileHandle {
    std::FILE* fp;
    explicit FileHandle(const char* path) : fp(std::fopen(path, "w")) {
        if (!fp) throw std::runtime_error("open failed");
    }
    ~FileHandle() {
        if (fp) std::fclose(fp);
    }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
};

void raii_demo() {
    Tracked t;
    FileHandle fh{"/tmp/raii_demo.txt"};
    throw std::runtime_error("boom");
}

// copy-and-swap：强异常保证
struct ThrowingCopy {
    static inline int copies_before_throw = 1;
    static inline int copies = 0;
    int value = 0;

    ThrowingCopy() = default;
    explicit ThrowingCopy(int v) : value(v) {}
    ThrowingCopy(const ThrowingCopy& other) : value(other.value) {
        if (++copies > copies_before_throw) throw std::runtime_error("copy failed");
    }
    ThrowingCopy(ThrowingCopy&&) noexcept = default;
    ThrowingCopy& operator=(const ThrowingCopy&) = default;
    ThrowingCopy& operator=(ThrowingCopy&&) noexcept = default;
};

class IntBuffer {
public:
    void push(int v) { data_.emplace_back(v); }

    void push_strong(int v) {
        IntBuffer tmp(*this);        // 拷贝可能抛异常，原对象不受影响
        tmp.data_.emplace_back(v);   // 追加可能抛异常，tmp 析构，原对象不变
        data_.swap(tmp.data_);       // swap 是 noexcept，提交
    }

    std::size_t size() const { return data_.size(); }
    int at(std::size_t i) const { return data_[i].value; }

private:
    std::vector<ThrowingCopy> data_;
};

template <typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F f) : f_(std::move(f)) {}
    ~ScopeGuard() {
        if (active_) f_();
    }
    void dismiss() noexcept { active_ = false; }
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    F f_;
    bool active_ = true;
};

bool g_committed = false;

void try_operation(bool fail) {
    ScopeGuard rollback{[] {
        if (!g_committed) std::printf("rollback: 回滚未提交的修改\n");
    }};
    if (fail) throw std::runtime_error("operation failed");
    g_committed = true;
    rollback.dismiss();
    std::printf("commit: 提交成功\n");
}

}  // namespace

int main() {
    std::printf("=== 1. RAII：异常路径也会析构 ===\n");
    try {
        raii_demo();
    } catch (const std::exception& e) {
        std::printf("caught: %s, live objects=%d\n", e.what(), g_live);
    }

    std::printf("\n=== 2. 强异常保证：copy-and-swap ===\n");
    IntBuffer buf;
    buf.push(1);
    buf.push(2);
    ThrowingCopy::copies = 0;
    ThrowingCopy::copies_before_throw = 1;  // 第二次拷贝抛异常
    try {
        buf.push_strong(3);
        std::printf("unexpected: push_strong 成功\n");
    } catch (const std::exception& e) {
        std::printf("push_strong 抛异常: %s, 原对象仍为 size=%zu [%d,%d]\n", e.what(), buf.size(),
                    buf.at(0), buf.at(1));
    }

    std::printf("\n=== 3. 基本保证：资源不泄漏，状态可能已改变 ===\n");
    {
        std::vector<ThrowingCopy> v;
        v.reserve(4);
        v.emplace_back(1);
        v.emplace_back(2);
        ThrowingCopy::copies = 0;
        ThrowingCopy::copies_before_throw = 0;  // 第一次拷贝就抛
        try {
            v.push_back(v[0]);
            std::printf("unexpected: push_back 成功\n");
        } catch (const std::exception& e) {
            std::printf("push_back 抛异常: %s（vector 仍有效，size=%zu）\n", e.what(), v.size());
        }
    }

    std::printf("\n=== 4. noexcept：move 是否可安全用于扩容 ===\n");
    std::printf("IntBuffer move noexcept=%d\n",
                static_cast<int>(std::is_nothrow_move_constructible_v<IntBuffer>));
    std::printf("ThrowingCopy move noexcept=%d\n",
                static_cast<int>(std::is_nothrow_move_constructible_v<ThrowingCopy>));

    std::printf("\n=== 5. ScopeGuard：异常回滚 / 正常提交 ===\n");
    try {
        try_operation(true);
    } catch (const std::exception& e) {
        std::printf("caught: %s\n", e.what());
    }
    g_committed = false;
    try_operation(false);

    std::printf("\n=== 6. 栈展开时的 uncaught_exceptions ===\n");
    struct Probe {
        int base = std::uncaught_exceptions();
        ~Probe() {
            std::printf("destructor: uncaught=%d, unwinding=%s\n",
                        std::uncaught_exceptions(),
                        std::uncaught_exceptions() > base ? "yes" : "no");
        }
    };
    try {
        Probe p;
        throw std::runtime_error("unwind");
    } catch (const std::exception& e) {
        std::printf("caught: %s\n", e.what());
    }
}
