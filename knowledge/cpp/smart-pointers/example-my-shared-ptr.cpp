// 追问二：手写一个 SharedPtr —— 对照 std::shared_ptr 的核心机制
// 编译: g++ -std=c++20 -O2 -Wall example-my-shared-ptr.cpp -o my_shared_ptr && ./my_shared_ptr
//
// 核心机制对照表（正文 §4.3）：
//   - 每个对象一个堆上"控制块"：强计数(原子) + 弱计数(原子) + 所指对象指针
//   - 拷贝  → 强计数原子 +1（relaxed 即可：计数本身不携带其他内存数据）
//   - 析构  → 强计数 acq_rel -1；减到 0 的线程负责 delete 对象
//   - 弱计数初始为 1（代表"全体强引用"集体的一份）：强计数归零只析构对象，
//     控制块要等弱计数也归零（无 weak_ptr 观察后才释放
//   - weak_ptr::lock() 用 CAS 原子地"检查强计数>0 并 +1"，防止检查与使用之间被并发析构
// 与 std 版的实现差异（教学版从简，注释标注）：
//   a) libstdc++ 把强/弱计数合并到一个字里做无锁优化；教学版用两个独立 atomic
//   b) std 支持类型擦除的自定义删除器/分配器（虚函数分派，仅构造/析构路径）；教学版只支持 delete
//   c) std::make_shared 把对象内嵌进控制块（一次分配）；教学版两次分配

#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

template <class T>
class MySharedPtr;

template <class T>
class MyWeakPtr;

// ---- 控制块 ----
template <class T>
struct MyControlBlock {
    std::atomic<long> strong{1};
    std::atomic<long> weak{1};   // 初始 1 = "全体强引用"集体持有的一份
    T* ptr;                      // 供 weak_ptr::lock() 提升时取用
    explicit MyControlBlock(T* p) noexcept : ptr(p) {}
    ~MyControlBlock() { std::cout << "    [控制块释放]\n"; }
};

// ---- 共享指针 ----
template <class T>
class MySharedPtr {
public:
    MySharedPtr() noexcept = default;

    explicit MySharedPtr(T* p) : ptr_(p), ctrl_(new MyControlBlock<T>(p)) {}
    // 注: std::make_shared 的单次分配（对象内嵌控制块）见正文 §4.3, 教学版不实现

    MySharedPtr(const MySharedPtr& o) noexcept : ptr_(o.ptr_), ctrl_(o.ctrl_) {
        if (ctrl_) ctrl_->strong.fetch_add(1, std::memory_order_relaxed);
    }

    MySharedPtr(MySharedPtr&& o) noexcept : ptr_(o.ptr_), ctrl_(o.ctrl_) {
        o.ptr_ = nullptr;
        o.ctrl_ = nullptr;
    }

    // 统一赋值: 按值传参(拷贝或移动构造临时) + swap, 被替换的旧引用随临时析构释放
    // 天然自赋值安全、异常安全(全程不抛)
    MySharedPtr& operator=(MySharedPtr o) noexcept {
        swap(o);
        return *this;
    }

    ~MySharedPtr() { release(); }

    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T* get() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    long use_count() const noexcept {
        return ctrl_ ? ctrl_->strong.load(std::memory_order_relaxed) : 0;
    }

    void swap(MySharedPtr& o) noexcept {
        std::swap(ptr_, o.ptr_);
        std::swap(ctrl_, o.ctrl_);
    }

    void reset(T* p = nullptr) { MySharedPtr(p).swap(*this); }

private:
    friend class MyWeakPtr<T>;

    void release() noexcept {
        if (!ctrl_) return;
        // acq_rel: 需要 acquire 侧看到其他线程对对象做的最后写入,
        //          需要 release 侧把本线程写入发布给随后析构该对象的线程
        if (ctrl_->strong.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete ptr_;  // 最后一个强引用: 析构对象
            if (ctrl_->weak.fetch_sub(1, std::memory_order_acq_rel) == 1)
                delete ctrl_;  // 强弱皆尽: 控制块退役
        }
        ptr_ = nullptr;
        ctrl_ = nullptr;
    }

    T* ptr_ = nullptr;
    MyControlBlock<T>* ctrl_ = nullptr;
};

// ---- 弱指针 ----
template <class T>
class MyWeakPtr {
public:
    MyWeakPtr() noexcept = default;

    MyWeakPtr(const MySharedPtr<T>& sp) noexcept : ptr_(sp.get()), ctrl_(sp.ctrl_) {
        if (ctrl_) ctrl_->weak.fetch_add(1, std::memory_order_relaxed);
    }
    MyWeakPtr(const MyWeakPtr& o) noexcept : ptr_(o.ptr_), ctrl_(o.ctrl_) {
        if (ctrl_) ctrl_->weak.fetch_add(1, std::memory_order_relaxed);
    }
    MyWeakPtr(MyWeakPtr&& o) noexcept : ptr_(o.ptr_), ctrl_(o.ctrl_) {
        o.ptr_ = nullptr;
        o.ctrl_ = nullptr;
    }
    MyWeakPtr& operator=(MyWeakPtr o) noexcept {
        swap(o);
        return *this;
    }
    ~MyWeakPtr() {
        if (ctrl_ && ctrl_->weak.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete ctrl_;  // 弱计数归零(且强计数早已归零) → 控制块释放
        ptr_ = nullptr;
        ctrl_ = nullptr;
    }

    long use_count() const noexcept {
        return ctrl_ ? ctrl_->strong.load(std::memory_order_relaxed) : 0;
    }
    bool expired() const noexcept { return use_count() == 0; }

    // 原子"检查并提升": 若强计数>0 则 +1 并返回共享指针; 为 0 则返回空
    // 必须是原子的, 否则"检查通过"与"使用"之间对象可能被其他线程析构(正文 §4.3)
    MySharedPtr<T> lock() const noexcept {
        MySharedPtr<T> sp;
        if (!ctrl_) return sp;
        long c = ctrl_->strong.load(std::memory_order_relaxed);
        while (c != 0 &&
               !ctrl_->strong.compare_exchange_weak(c, c + 1, std::memory_order_acquire)) {
        }
        if (c != 0) {
            sp.ptr_ = ctrl_->ptr;  // CAS 成功时对象必然存活
            sp.ctrl_ = ctrl_;
        }
        return sp;
    }

    void swap(MyWeakPtr& o) noexcept {
        std::swap(ptr_, o.ptr_);
        std::swap(ctrl_, o.ctrl_);
    }

private:
    T* ptr_ = nullptr;
    MyControlBlock<T>* ctrl_ = nullptr;
};

namespace {

struct Tracked {
    std::string name;
    explicit Tracked(std::string n) : name(std::move(n)) {}
    ~Tracked() { std::cout << "    ~Tracked " << name << "\n"; }
};

// ========== 实验一：拷贝 / 移动 / 析构的计数变化 ==========
void experiment_one() {
    std::cout << "[实验一] 拷贝/移动/析构时的计数变化\n";
    {
        MySharedPtr<Tracked> a(new Tracked("A"));
        std::cout << "  构造后 a.use_count=" << a.use_count() << "\n";
        {
            MySharedPtr<Tracked> b = a;             // 拷贝: strong 1→2
            std::cout << "  拷贝 b=a 后: a=" << a.use_count() << ", b=" << b.use_count() << "\n";
            MySharedPtr<Tracked> c = std::move(b);  // 移动: 计数不变, b 转为空
            std::cout << "  移动 c=std::move(b) 后: c=" << c.use_count() << ", b 为空? "
                      << std::boolalpha << !b << "\n";
        }  // c 析构: strong 2→1
        std::cout << "  内层作用域结束(析构了移动后的 c): a.use_count=" << a.use_count()
                  << ", 对象还没析构\n";
    }  // a 析构: strong 1→0 → ~Tracked A → 控制块释放
    std::cout << "  ↑ 外层作用域结束: 触发上面的 ~Tracked 与 [控制块释放]\n";
}

// ========== 实验二：weak 观察（对象析构不受阻，控制块由弱计数守护） ==========
void experiment_two() {
    std::cout << "[实验二] MyWeakPtr: 对象析构不受阻, 控制块活得比对象久\n";
    MyWeakPtr<Tracked> wk;
    {
        MySharedPtr<Tracked> sp(new Tracked("B"));
        wk = sp;  // 拷贝: weak 计数 1→2(集体份额+观察者)
        std::cout << "  sp.use_count=" << sp.use_count() << ", wk.expired=" << wk.expired() << "\n";
        auto promoted = wk.lock();  // CAS 提升: 强计数 1→2
        std::cout << "  lock() 提升成功: name=" << promoted->name
                  << ", use_count=" << promoted.use_count() << "\n";
    }  // sp/promoted 析构: strong→0 → ~Tracked B; weak 仍=1(观察者) → 控制块暂不释放
    std::cout << "  ↑ 作用域结束: 对象已析构, 但控制块尚未释放(弱计数守护)\n";
    std::cout << "  wk.expired=" << std::boolalpha << wk.expired()
              << ", lock() 返回空? " << !wk.lock() << "\n";
}  // wk 析构: weak→0 → [控制块释放]
// 注意: "[控制块释放]" 在本函数最后一行输出之后才打印

// ========== 实验三：循环引用（与 std::shared_ptr 行为一致） ==========
struct MyBadNode {
    std::string name;
    MySharedPtr<MyBadNode> peer;  // 双强引用 → 环
    explicit MyBadNode(std::string n) : name(std::move(n)) {}
    ~MyBadNode() { std::cout << "    ~MyBadNode " << name << "（双强引用时永不打印）\n"; }
};

struct MyGoodNode {
    std::string name;
    MySharedPtr<MyGoodNode> next;
    MyWeakPtr<MyGoodNode> prev;  // 回边 weak → 破环
    explicit MyGoodNode(std::string n) : name(std::move(n)) {}
    ~MyGoodNode() { std::cout << "    ~MyGoodNode " << name << "\n"; }
};

void experiment_three() {
    std::cout << "[实验三] 循环引用: 手写版同样泄漏, 同样靠 weak 破环\n";
    {
        auto a = MySharedPtr<MyBadNode>(new MyBadNode("A"));
        auto b = MySharedPtr<MyBadNode>(new MyBadNode("B"));
        a->peer = b;
        b->peer = a;
        std::cout << "  互指后: a.use_count=" << a.use_count() << ", b.use_count=" << b.use_count() << "\n";
    }
    std::cout << "  ↑ 无 ~MyBadNode 输出: 强计数互相顶住, 泄漏(与 std 行为一致)\n";
    {
        auto x = MySharedPtr<MyGoodNode>(new MyGoodNode("X"));
        auto y = MySharedPtr<MyGoodNode>(new MyGoodNode("Y"));
        x->next = y;
        y->prev = x;
        std::cout << "  next/prev 后: x.use_count=" << x.use_count() << ", y.use_count=" << y.use_count() << "\n";
    }
    std::cout << "  ↑ weak 回边不顶强计数, 环被打破\n";
}

// ========== 实验四：多线程下计数原子性 ==========
void experiment_four() {
    std::cout << "[实验四] 4 线程各拷贝/析构副本 10 万次 → 原子计数无丢失\n";
    MySharedPtr<int> sp(new int(0));
    std::vector<std::thread> pool;
    pool.reserve(4);
    for (int t = 0; t < 4; ++t) {
        pool.emplace_back([sp] {  // 按值捕获 = 拷贝, strong +1
            for (int i = 0; i < 100000; ++i) {
                auto local = sp;  // +1; local 析构 -1
            }
        });
    }
    for (auto& t : pool) t.join();
    std::cout << "  join 后 use_count=" << sp.use_count() << "（期望 1）\n";
}

// ========== 实验五：尺寸对照 ==========
void experiment_five() {
    std::cout << "[实验五] 尺寸对照: 我们的手写版与 std 一致\n";
    std::cout << "  sizeof(MySharedPtr<int>)     = " << sizeof(MySharedPtr<int>) << "  (指针×2, 控制块在堆上)\n";
    std::cout << "  sizeof(std::shared_ptr<int>) = " << sizeof(std::shared_ptr<int>) << "  (同样指针×2)\n";
    static_assert(sizeof(MySharedPtr<int>) == 2 * sizeof(void*));
}

}  // namespace

int main() {
    std::cout << "== 追问二: 手写 SharedPtr ==\n";
    experiment_one();
    experiment_two();
    experiment_three();
    experiment_four();
    experiment_five();
    return 0;
}
